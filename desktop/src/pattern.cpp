// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/pattern.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "lib/ecore/math.h"

// The relic's own pattern source, compiled for the host. Not a copy: this is
// the same file the obelisk runs, and it needed no changes to get here.
#include "relics/obelisk/state_obelisk.h"

#include "edmx/mythos26.h"
#include "edmx/state_machine.h"

using namespace edmx;

namespace
{
    /// Every fixture the same colour. The one you want when someone says
    /// "just make it warm white".
    class SolidPattern : public Pattern
    {
    public:
        const char* getName() const override { return "solid"; }

        void tick(float deltaTime) override { (void)deltaTime; }

        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override
        {
            ecore::HSV out = color;
            out.setBrightnessAlpha(out.getValFloat() * brightness);
            outColors.assign(context.fixtureCount, out);
        }
    };


    /// The palette swept across the rig. This is the workhorse: an eanim LFO
    /// supplies the moving 0..1 value and HSVPalette turns it into colour, so
    /// the look is identical to the strip version of the same pattern.
    class PaletteWavePattern : public Pattern
    {
    public:
        const char* getName() const override { return "palette_wave"; }

        void tick(float deltaTime) override
        {
            lfo.speed = speed;
            lfo.width = width;
            lfo.tick(deltaTime);
        }

        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override
        {
            outColors.resize(context.fixtureCount);
            for (size_t idx = 0; idx < context.fixtureCount; ++idx)
            {
                const float position = (idx < context.positions.size()) ? context.positions[idx] : 0.0f;
                const float t = lfo.evaluate(position);

                ecore::HSV out = palette.getColor(t);
                out.setBrightnessAlpha(out.getValFloat() * brightness);
                outColors[idx] = out;
            }
        }

    private:
        eanim::LFO lfo;
    };


    /// Hue ramp across the rig, rotating over time. No palette involved: this
    /// is the sanity-check pattern, and a rig showing anything other than a
    /// clean spectrum here has its channel order wrong.
    class RainbowPattern : public Pattern
    {
    public:
        const char* getName() const override { return "rainbow"; }

        void tick(float deltaTime) override
        {
            hueOffset += deltaTime * speed * 360.0f;
            hueOffset = std::fmod(hueOffset, 360.0f);
        }

        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override
        {
            outColors.resize(context.fixtureCount);
            for (size_t idx = 0; idx < context.fixtureCount; ++idx)
            {
                const float position = (idx < context.positions.size()) ? context.positions[idx] : 0.0f;
                const float hue = std::fmod(hueOffset + (position * 360.0f / std::max(width, 0.001f)), 360.0f);
                outColors[idx] = ecore::HSV(hue, 1.0f, brightness);
            }
        }

    private:
        float hueOffset{0.0f};
    };


    /// A lit fixture running along the rig, driven by eanim's Saw. Reads as a
    /// chase because the saw resets rather than reflecting.
    class ChasePattern : public Pattern
    {
    public:
        const char* getName() const override { return "chase"; }

        ChasePattern()
        {
            // one saw cycle == one lap of the rig, which is what lets us treat
            // the saw's output directly as a normalised position below.
            saw.width = 1.0f;
            saw.gapWidth = 0.0f;
        }

        void tick(float deltaTime) override
        {
            saw.speed = speed;
            saw.tick(deltaTime);
        }

        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override
        {
            outColors.assign(context.fixtureCount, ecore::HSV(0.0f, 0.0f, 0.0f));
            if (context.fixtureCount == 0)
            {
                return;
            }

            // Saw ramps 1 -> 0 over a cycle; invert it so the head sweeps
            // forward along the rig the way the eye expects a chase to run.
            const float head = 1.0f - saw.evaluate(0.0f);

            // width is in fixtures here, and it is the length of the tail.
            const float tail = std::max(width, 0.001f) / static_cast<float>(context.fixtureCount);

            for (size_t idx = 0; idx < context.fixtureCount; ++idx)
            {
                const float position = (idx < context.positions.size()) ? context.positions[idx] : 0.0f;

                // shortest distance behind the head, wrapping around the rig
                float distance = head - position;
                if (distance < 0.0f)
                {
                    distance += 1.0f;
                }

                if (distance > tail)
                {
                    continue;
                }

                const float falloff = 1.0f - (distance / tail);
                ecore::HSV out = palette.getColor(position);
                out.setBrightnessAlpha(out.getValFloat() * falloff * brightness);
                outColors[idx] = out;
            }
        }

    private:
        eanim::Saw saw;
    };


    /// Whole rig breathing on one colour. Same LFO, evaluated at a single
    /// point instead of across the rig.
    class PulsePattern : public Pattern
    {
    public:
        const char* getName() const override { return "pulse"; }

        void tick(float deltaTime) override
        {
            lfo.speed = speed;
            lfo.tick(deltaTime);
        }

        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override
        {
            const float level = lfo.evaluate(0.0f);

            ecore::HSV out = color;
            out.setBrightnessAlpha(out.getValFloat() * level * brightness);
            outColors.assign(context.fixtureCount, out);
        }

    private:
        eanim::LFO lfo;
    };


    /// Lights one fixture at a time, in patch order, holding each for a beat.
    ///
    /// The commissioning tool. On a rig of ten identical pars the only way to
    /// know that fixture 7 in the config is the seventh one on the truss is to
    /// light it alone and go look. `speed` is fixtures per second.
    class IdentifyPattern : public Pattern
    {
    public:
        const char* getName() const override { return "identify"; }

        void tick(float deltaTime) override
        {
            elapsed += deltaTime * std::max(speed, 0.01f);
        }

        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override
        {
            outColors.assign(context.fixtureCount, ecore::HSV(0.0f, 0.0f, 0.0f));
            if (context.fixtureCount == 0)
            {
                return;
            }

            const size_t active = static_cast<size_t>(elapsed) % context.fixtureCount;

            // white, so a wrong channel order shows up as a colour cast rather
            // than hiding behind a hue that happens to look plausible
            outColors[active] = ecore::HSV(0.0f, 0.0f, brightness);
        }

    private:
        float elapsed{0.0f};
    };


    /// Everything off. Worth having as a real pattern rather than a special
    /// case: "blackout" should behave like any other look.
    class OffPattern : public Pattern
    {
    public:
        const char* getName() const override { return "off"; }

        void tick(float deltaTime) override { (void)deltaTime; }

        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override
        {
            outColors.assign(context.fixtureCount, ecore::HSV(0.0f, 0.0f, 0.0f));
        }
    };
}

// ============================================================================
// GeneratorHSV adapter
// ============================================================================

GeneratorPattern::GeneratorPattern(const char* inName, std::shared_ptr<eanim::GeneratorHSV> inGenerator)
    : name(inName), generator(std::move(inGenerator))
{
}

void GeneratorPattern::ensureNodes(const PatternContext& context)
{
    if (nodes.size() == context.fixtureCount && strip)
    {
        return;
    }

    strip.reset(new eio::HSVStrip(static_cast<uint16_t>(context.fixtureCount), 0));
    segment.reset(new eio::HSVStripSegment(strip.get(), 0));
    nodes.clear();

    for (size_t idx = 0; idx < context.fixtureCount; ++idx)
    {
        auto node = std::make_shared<eio::HSVStripNode_Mapped2D>(segment.get(), static_cast<int>(idx));

        // Give the generator a coordinate space it recognises, by running the
        // rig across it. x picks up anything the pattern keys off sides or
        // columns; y gives a noise field the range it needs to not read as
        // flat colour.
        const float position = (idx < context.positions.size()) ? context.positions[idx] : 0.0f;
        node->coord = context.coords.at(position);

        nodes.push_back(node);
    }
}

void GeneratorPattern::tick(float deltaTime)
{
    generator->tick(deltaTime);
}

void GeneratorPattern::render(const PatternContext& context, std::vector<ecore::HSV>& outColors)
{
    ensureNodes(context);

    outColors.resize(context.fixtureCount);
    for (size_t idx = 0; idx < context.fixtureCount; ++idx)
    {
        ecore::HSV color;
        generator->render(nodes[idx].get(), color);

        color.setBrightnessAlpha(color.getValFloat() * brightness);
        outColors[idx] = color;
    }
}

// ============================================================================
// Registry
// ============================================================================

namespace
{
    /// name -> factory. Ordered so --list-patterns is stable, and a map so a
    /// later registration of the same name replaces an earlier one.
    std::map<std::string, PatternFactory>& registry()
    {
        static std::map<std::string, PatternFactory> instance;
        return instance;
    }

    /// Registers the shipped patterns on first use.
    ///
    /// Adding one is a single line here. The relic entries wrap a GeneratorHSV
    /// straight out of the relic's own source, which is what keeps looks
    /// shared between the microcontrollers and this.
    void ensureBuiltinsRegistered()
    {
        static bool done = false;
        if (done)
        {
            return;
        }
        done = true;

        auto& table = registry();

        table["solid"]        = []() { return std::unique_ptr<Pattern>(new SolidPattern()); };
        table["palette_wave"] = []() { return std::unique_ptr<Pattern>(new PaletteWavePattern()); };
        table["rainbow"]      = []() { return std::unique_ptr<Pattern>(new RainbowPattern()); };
        table["chase"]        = []() { return std::unique_ptr<Pattern>(new ChasePattern()); };
        table["pulse"]        = []() { return std::unique_ptr<Pattern>(new PulsePattern()); };
        table["identify"]     = []() { return std::unique_ptr<Pattern>(new IdentifyPattern()); };
        table["off"]          = []() { return std::unique_ptr<Pattern>(new OffPattern()); };

        // --- relic state machines -------------------------------------------
        // A whole relic's worth of looks, with its own transitions between
        // them. Switch state with the `state` command; see makeJacketStateMachine.
        table["jacket"] = []() {
            return std::unique_ptr<Pattern>(makeJacketStateMachine().release());
        };

        // --- the show -------------------------------------------------------
        // Written for the rig rather than borrowed from a relic, and the first
        // thing here that reads the beat. See makeMythos26StateMachine.
        table["mythos26"] = []() {
            return std::unique_ptr<Pattern>(makeMythos26StateMachine().release());
        };

        // --- relic patterns, unmodified -------------------------------------
        table["obelisk_seasons"] = []() {
            return std::unique_ptr<Pattern>(new GeneratorPattern(
                "obelisk_seasons", std::make_shared<Pattern_Obelisk_FourSeasons>()));
        };
        table["obelisk_theater"] = []() {
            return std::unique_ptr<Pattern>(new GeneratorPattern(
                "obelisk_theater", std::make_shared<Pattern_Obelisk_Theater>()));
        };
        table["obelisk_mono"] = []() {
            return std::unique_ptr<Pattern>(new GeneratorPattern(
                "obelisk_mono", std::make_shared<Pattern_Obelisk_Monocolor>()));
        };
    }
}

void edmx::registerPattern(const std::string& name, PatternFactory factory)
{
    ensureBuiltinsRegistered();
    registry()[name] = std::move(factory);
}

ecore::HSVPalette edmx::resolvePalette(const PatternConfig& config)
{
    // Explicit stops win over a name: if someone listed colours, honour them.
    if (config.palette.size() >= 2)
    {
        return ecore::HSVPalette(config.palette);
    }

    ecore::HSVPalette named;
    if (lookupNamedPalette(config.paletteName, named))
    {
        return named;
    }

    // Last resort so a typo'd palette name still lights the rig.
    return ecore::HSVPalette({
        ecore::HSV(0.0f, 1.0f, 1.0f),
        ecore::HSV(240.0f, 1.0f, 1.0f),
    });
}

std::vector<std::string> edmx::patternNames()
{
    ensureBuiltinsRegistered();

    std::vector<std::string> names;
    for (const auto& entry : registry())
    {
        names.push_back(entry.first);
    }
    return names;
}

std::unique_ptr<Pattern> edmx::makePattern(const std::string& name,
                                           const PatternConfig& config,
                                           std::string& outError)
{
    ensureBuiltinsRegistered();

    auto it = registry().find(name);
    if (it == registry().end())
    {
        std::string known;
        for (const std::string& candidate : patternNames())
        {
            known += (known.empty() ? "" : ", ") + candidate;
        }
        outError = "unknown pattern '" + name + "' (available: " + known + ")";
        return nullptr;
    }

    std::unique_ptr<Pattern> pattern = it->second();
    if (!pattern)
    {
        outError = "pattern '" + name + "' failed to build";
        return nullptr;
    }

    pattern->setSpeed(config.speed);
    pattern->setWidth(config.width);
    pattern->setBrightness(config.brightness);
    pattern->setPalette(resolvePalette(config));
    pattern->setColor(config.solidColor);
    return pattern;
}
