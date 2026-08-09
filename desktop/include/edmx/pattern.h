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
    /// Everything a pattern is allowed to know about the rig.
    struct PatternContext
    {
        size_t fixtureCount{0};
        /// Position of each fixture along the rig, 0..1, index-aligned.
        std::vector<float> positions;

        /// Coordinate space handed to relic patterns via GeneratorPattern.
        ///
        /// Relic patterns were written against a physical layout, and a noise
        /// field tuned for that scale reads as flat colour if you hand it 0..1.
        /// So a rig's normalised positions get stretched across this span.
        ///
        /// The defaults project a line of fixtures diagonally across the
        /// obelisk's space: 8 columns wide (its four sides, two strips each,
        /// which is where its per-side palettes come from) and 43 tall (one
        /// strip, which is where its noise gets most of its variation). A rig
        /// therefore picks up both the side palettes and real spatial motion.
        ///
        /// Override per-config with pattern.coord_span_x / _y when a rig wants
        /// a different slice of that space.
        float coordSpanX{8.0f};
        float coordSpanY{43.0f};
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
