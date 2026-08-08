// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lib/ecore/hsv.h"
#include "lib/ecore/tickable.h"
#include "lib/eanim/lfo.h"
#include "lib/eanim/generator_float.h"

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

    /// Builds the pattern named by `name`, seeded from `config`. Returns
    /// nullptr and fills outError when the name is not one we ship.
    std::unique_ptr<Pattern> makePattern(const std::string& name,
                                         const PatternConfig& config,
                                         std::string& outError);

    /// Every pattern name, for --list-patterns and error messages.
    std::vector<std::string> patternNames();

    /// Resolves config.palette / config.paletteName into a usable palette.
    ecore::HSVPalette resolvePalette(const PatternConfig& config);
}
