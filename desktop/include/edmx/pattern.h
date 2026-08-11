// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lib/ecore/hsv.h"
#include "lib/ecore/tickable.h"
#include "lib/eanim/lfo.h"
#include "lib/eanim/generator_float.h"
#include "lib/eanim/generator_hsv.h"
#include "lib/eio/hsv_strip.h"
#include "lib/eio/strip_projection.h"

#include "edmx/config.h"

///
/// Patterns.
///
/// These are deliberately thin. All the actual colour and motion work is done
/// by ecore::HSVPalette and the eanim generators, exactly as it is on the
/// microcontroller: the desktop build gets to be a different renderer for the
/// same library, not a reimplementation of it.
///

namespace edmx
{
    /// Where a rig sits in the coordinate space a relic pattern expects.
    ///
    /// A node's coordinate is `origin + position * span`, position being the
    /// fixture's 0..1 place along the rig. Relic patterns were written against
    /// a physical layout, and a field tuned for that scale reads as flat colour
    /// if you hand it 0..1 — so a rig's normalised positions get stretched
    /// across this frame.
    ///
    /// Two numbers per axis rather than one because relic patterns do not agree
    /// on an origin. The obelisk's looks read a noise field from (0,0) across
    /// 8 x 43 — 8 being its four sides at two strips each, which is where its
    /// per-side palettes come from, and 43 being one strip's height. The
    /// jacket's monowire instead runs up a near-vertical line starting at
    /// (0.75, -0.25), and every jacket look is tuned around that.
    ///
    /// Each pattern declares the frame it was written for; a config overrides
    /// any of the four when a rig wants a different slice.
    struct CoordFrame
    {
        float originX{0.0f};
        float originY{0.0f};
        float spanX{8.0f};
        float spanY{43.0f};

        /// The coordinate for a fixture at `position` along the rig.
        ecore::Coordinate at(float position) const
        {
            return ecore::Coordinate{originX + position * spanX,
                                     originY + position * spanY};
        }
    };

    /// Everything a pattern is allowed to know about the rig.
    struct PatternContext
    {
        size_t fixtureCount{0};
        /// Position of each fixture along the rig, 0..1, index-aligned.
        std::vector<float> positions;

        /// Resolved for the running pattern: its own default frame, with any
        /// config override applied on top.
        CoordFrame coords;
    };

    class Pattern : public ecore::Tickable
    {
    public:
        virtual ~Pattern() = default;

        virtual const char* getName() const = 0;

        /// Advance internal generators.
        void tick(float deltaTime) override = 0;

        /// Fill `outColors` with one HSV per fixture.
        virtual void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) = 0;

        /// The coordinate space this pattern was written against. The built-in
        /// patterns work off `positions` directly and do not care; relic
        /// patterns very much do. A config can override any field of it.
        virtual CoordFrame defaultCoordFrame() const { return CoordFrame{}; }

        /// Non-null when this pattern is a state machine, so the protocol can
        /// offer `state` for it. A virtual rather than a dynamic_cast because
        /// the library builds without RTTI on the microcontroller side and
        /// there is no reason for the two to diverge.
        virtual class StateMachinePattern* asStateMachine() { return nullptr; }

        /// The knobs this pattern offers, gathered fresh on every call.
        ///
        /// Fresh, and never cached, because a PropertyBag holds pointers into
        /// whatever filled it — and on a state machine that is the running
        /// look, which changes underneath you on a cue. Rebuilding it costs a
        /// handful of allocations on a command, not on a frame.
        ///
        /// The base offers the three every pattern has. A look with its own
        /// knobs overrides this, calls up, and adds them; one that has no use
        /// for speed and width overrides without calling up.
        virtual void reflect(ecore::PropertyBag& bag);

        /// Live control, driven by the stdin protocol. Values are applied
        /// immediately so the python wrapper can nudge a running show.
        virtual void setSpeed(float value) { speed = value; }
        virtual void setWidth(float value) { width = value; }
        virtual void setBrightness(float value) { brightness = value; }
        virtual void setPalette(const ecore::HSVPalette& value) { palette = value; }
        virtual void setColor(const ecore::HSV& value) { color = value; }

        float getSpeed() const { return speed; }
        float getWidth() const { return width; }
        float getBrightness() const { return brightness; }

    protected:
        float speed{0.25f};
        float width{2.0f};
        float brightness{1.0f};
        ecore::HSVPalette palette;
        ecore::HSV color{0.0f, 1.0f, 1.0f};
    };

    /// Runs any eclipse-os GeneratorHSV as a DMX pattern.
    ///
    /// This is the hot-swap seam. A relic pattern is a GeneratorHSV that reads
    /// a node's 2D coordinate and writes a colour; it does not care whether the
    /// node is an LED on a strip or a par on a truss. So we build one
    /// HSVStripNode_Mapped2D per fixture, hand them to the generator, and every
    /// pattern ever written for a relic runs here unmodified.
    ///
    /// No porting, no copies, one source of truth for what a look is.
    class GeneratorPattern : public Pattern
    {
    public:
        GeneratorPattern(const char* inName, std::shared_ptr<eanim::GeneratorHSV> inGenerator);

        const char* getName() const override { return name.c_str(); }

        void tick(float deltaTime) override;
        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override;

        /// The wrapped generator, for anything that needs to poke at it.
        eanim::GeneratorHSV* getGenerator() const { return generator.get(); }

        /// Straight through to the generator: a relic look's knobs are the
        /// look's, not the wrapper's, and speed/width mean nothing here.
        void reflect(ecore::PropertyBag& bag) override;

    private:
        /// Rebuilds the node set when the rig changes shape.
        void ensureNodes(const PatternContext& context);

        std::string name;
        std::shared_ptr<eanim::GeneratorHSV> generator;

        // The generator writes through nodes, so we need a strip behind them.
        // It is never shown; it exists to give the nodes somewhere to live.
        std::unique_ptr<eio::HSVStrip> strip;
        std::unique_ptr<eio::HSVStripSegment> segment;
        std::vector<std::shared_ptr<eio::HSVStripNode_Mapped2D>> nodes;
    };


    /// Makes a pattern from scratch. Registered under a name.
    using PatternFactory = std::function<std::unique_ptr<Pattern>()>;

    /// Adds a pattern to the registry, or replaces one of the same name.
    ///
    /// Registration is how a pattern becomes available: there is no switch
    /// statement to edit. Call this before makePattern and the name is live.
    void registerPattern(const std::string& name, PatternFactory factory);

    /// Builds the pattern named by `name`, seeded from `config`. Returns
    /// nullptr and fills outError when the name is not registered.
    std::unique_ptr<Pattern> makePattern(const std::string& name,
                                         const PatternConfig& config,
                                         std::string& outError);

    /// Every registered pattern name, for --list-patterns and error messages.
    std::vector<std::string> patternNames();

    /// Resolves config.palette / config.paletteName into a usable palette.
    ecore::HSVPalette resolvePalette(const PatternConfig& config);
}
