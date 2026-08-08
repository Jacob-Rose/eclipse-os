// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/fixture.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

using namespace edmx;

Rgb8 edmx::hsvToRgb8(const ecore::HSV& hsv)
{
    // Standard HSV -> RGB sector walk. We pull floats out of the library's
    // fixed-point representation here rather than reimplementing the fixed
    // point maths: this runs once per fixture per frame on a desktop CPU, so
    // the precision is worth far more than the cycles.
    const float h = hsv.getHueFloat();   // 0..360
    const float s = hsv.getSatFloat();   // 0..1
    const float v = hsv.getValFloat();   // 0..1

    const float chroma = v * s;
    const float sector = std::fmod(h < 0.0f ? h + 360.0f : h, 360.0f) / 60.0f;
    const float x = chroma * (1.0f - std::fabs(std::fmod(sector, 2.0f) - 1.0f));
    const float m = v - chroma;

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    switch (static_cast<int>(sector))
    {
        case 0: r = chroma; g = x;      b = 0.0f;   break;
        case 1: r = x;      g = chroma; b = 0.0f;   break;
        case 2: r = 0.0f;   g = chroma; b = x;      break;
        case 3: r = 0.0f;   g = x;      b = chroma; break;
        case 4: r = x;      g = 0.0f;   b = chroma; break;
        default:r = chroma; g = 0.0f;   b = x;      break;
    }

    const auto toByte = [](float value) -> uint8_t {
        const float scaled = std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f);
        return static_cast<uint8_t>(scaled);
    };

    Rgb8 out;
    out.r = toByte(r + m);
    out.g = toByte(g + m);
    out.b = toByte(b + m);
    return out;
}

uint8_t edmx::applyGamma(uint8_t value, float gamma)
{
    if (gamma <= 0.0f || std::fabs(gamma - 1.0f) < 0.001f)
    {
        return value;
    }

    const float normalized = static_cast<float>(value) / 255.0f;
    const float corrected = std::pow(normalized, gamma);
    return static_cast<uint8_t>(std::round(std::clamp(corrected, 0.0f, 1.0f) * 255.0f));
}

bool edmx::resolveChannelOrder(Fixture& fixture, std::string& outError)
{
    std::string order = fixture.channelOrder;
    std::transform(order.begin(), order.end(), order.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (order.size() != 3)
    {
        outError = "fixture '" + fixture.name + "': channels must be three letters, got '" + fixture.channelOrder + "'";
        return false;
    }

    int found[3] = {-1, -1, -1};
    for (int i = 0; i < 3; ++i)
    {
        switch (order[static_cast<size_t>(i)])
        {
            case 'r': found[0] = i; break;
            case 'g': found[1] = i; break;
            case 'b': found[2] = i; break;
            default:
                outError = "fixture '" + fixture.name + "': channels must be a permutation of r, g and b, got '"
                         + fixture.channelOrder + "'";
                return false;
        }
    }

    if (found[0] < 0 || found[1] < 0 || found[2] < 0)
    {
        outError = "fixture '" + fixture.name + "': channels '" + fixture.channelOrder
                 + "' is missing one of r, g or b";
        return false;
    }

    fixture.offsetR = found[0];
    fixture.offsetG = found[1];
    fixture.offsetB = found[2];
    fixture.channelOrder = order;
    return true;
}

// ============================================================================
// Fixture profiles
// ============================================================================

namespace
{
    /// The shipped profiles.
    ///
    /// `park` entries are what makes these worth having: a cheap par will sit
    /// dark, or strobe, or run its own colour macro and ignore you entirely,
    /// until its mode channels are pinned. Encoding that per model means it is
    /// solved once instead of every time someone patches a rig.
    const std::vector<FixtureProfile>& builtinProfiles()
    {
        static const std::vector<FixtureProfile> profiles = []() {
            std::vector<FixtureProfile> out;

            {
                // U'King Par 36, 8-channel mode.
                //   1 master dimmer   2 red   3 green   4 blue
                //   5 strobe          6 mode  7 colour selection
                //   8 (speed on these; 0 is correct either way)
                FixtureProfile profile;
                profile.name = "uking_par36";
                profile.footprint = 8;
                profile.dimmerOffset = 1;
                profile.dimmerValue = 255;
                profile.redOffset = 2;
                profile.greenOffset = 3;
                profile.blueOffset = 4;
                profile.park = {{5, 0}, {6, 0}, {7, 0}, {8, 0}};
                out.push_back(profile);
            }

            {
                // The generic 3-channel par: nothing but colour.
                FixtureProfile profile;
                profile.name = "rgb3";
                profile.footprint = 3;
                profile.redOffset = 1;
                profile.greenOffset = 2;
                profile.blueOffset = 3;
                out.push_back(profile);
            }

            {
                // Dimmer in front of the colours, no mode channels.
                FixtureProfile profile;
                profile.name = "rgb4_dimmer";
                profile.footprint = 4;
                profile.dimmerOffset = 1;
                profile.redOffset = 2;
                profile.greenOffset = 3;
                profile.blueOffset = 4;
                out.push_back(profile);
            }

            {
                // The other very common cheap-par layout: colour first, then
                // dimmer, strobe and a macro channel.
                FixtureProfile profile;
                profile.name = "rgb7_par";
                profile.footprint = 7;
                profile.redOffset = 1;
                profile.greenOffset = 2;
                profile.blueOffset = 3;
                profile.dimmerOffset = 4;
                profile.park = {{5, 0}, {6, 0}, {7, 0}};
                out.push_back(profile);
            }

            return out;
        }();

        return profiles;
    }
}

bool edmx::lookupBuiltinProfile(const std::string& name, FixtureProfile& outProfile)
{
    for (const FixtureProfile& profile : builtinProfiles())
    {
        if (profile.name == name)
        {
            outProfile = profile;
            return true;
        }
    }
    return false;
}

std::vector<std::string> edmx::builtinProfileNames()
{
    std::vector<std::string> names;
    for (const FixtureProfile& profile : builtinProfiles())
    {
        names.push_back(profile.name);
    }
    return names;
}

std::string edmx::describeProfile(const FixtureProfile& profile)
{
    std::vector<std::string> slots(static_cast<size_t>(std::max(profile.footprint, 1)), "-");

    const auto label = [&](int offset, const std::string& text) {
        if (offset >= 1 && offset <= profile.footprint)
        {
            slots[static_cast<size_t>(offset - 1)] = text;
        }
    };

    label(profile.redOffset, "red");
    label(profile.greenOffset, "green");
    label(profile.blueOffset, "blue");
    if (profile.dimmerOffset > 0)
    {
        label(profile.dimmerOffset, "dimmer=" + std::to_string(static_cast<int>(profile.dimmerValue)));
    }
    for (const auto& entry : profile.park)
    {
        label(entry.first, "park=" + std::to_string(static_cast<int>(entry.second)));
    }

    std::string out = profile.name + " (" + std::to_string(profile.footprint) + "ch)";
    for (size_t i = 0; i < slots.size(); ++i)
    {
        out += "  " + std::to_string(i + 1) + ":" + slots[i];
    }
    return out;
}

bool edmx::validateProfile(const FixtureProfile& profile, std::string& outError)
{
    const std::string where = "profile '" + profile.name + "'";

    if (profile.footprint < 1 || profile.footprint > DMX_CHANNEL_COUNT)
    {
        outError = where + ": footprint " + std::to_string(profile.footprint) + " is out of range";
        return false;
    }

    // offset -> what claimed it, so a double-booked channel names both sides
    std::map<int, std::string> claimed;

    const auto claim = [&](int offset, const std::string& what) -> bool {
        if (offset < 1 || offset > profile.footprint)
        {
            outError = where + ": " + what + " is at channel " + std::to_string(offset)
                     + ", outside the " + std::to_string(profile.footprint) + "-channel footprint";
            return false;
        }
        auto it = claimed.find(offset);
        if (it != claimed.end())
        {
            outError = where + ": channel " + std::to_string(offset) + " is used by both "
                     + it->second + " and " + what;
            return false;
        }
        claimed[offset] = what;
        return true;
    };

    if (!claim(profile.redOffset, "red"))     return false;
    if (!claim(profile.greenOffset, "green")) return false;
    if (!claim(profile.blueOffset, "blue"))   return false;

    if (profile.dimmerOffset > 0 && !claim(profile.dimmerOffset, "dimmer"))
    {
        return false;
    }

    for (const auto& entry : profile.park)
    {
        if (!claim(entry.first, "a parked channel"))
        {
            return false;
        }
    }

    return true;
}

Fixture edmx::instantiateProfile(const FixtureProfile& profile,
                                 const std::string& name,
                                 int address)
{
    Fixture fixture;
    fixture.name = name;

    // startChannel is the fixture's own address and the colour offsets are
    // relative to it, which lets a profile place r/g/b anywhere in the
    // footprint rather than requiring them to be three in a row.
    fixture.startChannel = address;
    fixture.offsetR = profile.redOffset - 1;
    fixture.offsetG = profile.greenOffset - 1;
    fixture.offsetB = profile.blueOffset - 1;

    // channelOrder is only a display string once a profile is driving things;
    // keep it honest for logs.
    fixture.channelOrder = "profile:" + profile.name;

    if (profile.dimmerOffset > 0)
    {
        fixture.dimmerChannel = address + profile.dimmerOffset - 1;
        fixture.dimmerValue = profile.dimmerValue;
    }

    for (const auto& entry : profile.park)
    {
        fixture.staticChannels.emplace_back(address + entry.first - 1, entry.second);
    }

    return fixture;
}

int edmx::fixtureHighestChannel(const Fixture& fixture)
{
    // the colour offsets are not necessarily 0/1/2 once a profile is placing
    // them, so take the actual maximum rather than assuming three in a row
    int highest = fixture.startChannel + std::max(fixture.offsetR,
                                                  std::max(fixture.offsetG, fixture.offsetB));
    highest = std::max(highest, fixture.dimmerChannel);
    for (const auto& entry : fixture.staticChannels)
    {
        highest = std::max(highest, entry.first);
    }
    return highest;
}

float FixtureMap::normalizedPosition(size_t idx) const
{
    if (idx >= fixtures.size())
    {
        return 0.0f;
    }

    if (fixtures[idx].hasPosition)
    {
        return fixtures[idx].positionX;
    }

    if (fixtures.size() <= 1)
    {
        return 0.0f;
    }
    return static_cast<float>(idx) / static_cast<float>(fixtures.size() - 1);
}

void FixtureMap::render(const std::vector<ecore::HSV>& colors,
                        float masterBrightness,
                        float gamma,
                        DmxUniverse& universe) const
{
    const float master = std::clamp(masterBrightness, 0.0f, 1.0f);

    for (size_t idx = 0; idx < fixtures.size(); ++idx)
    {
        const Fixture& fixture = fixtures[idx];

        // Park the mode/strobe channels first so a fixture that needs one held
        // at a value gets it even on the very first frame.
        for (const auto& entry : fixture.staticChannels)
        {
            universe.setChannel(entry.first, entry.second);
        }

        if (fixture.dimmerChannel > 0)
        {
            universe.setChannel(fixture.dimmerChannel, fixture.dimmerValue);
        }

        ecore::HSV color;
        if (idx < colors.size())
        {
            color = colors[idx];
        }

        // Scale in HSV before the conversion: dimming value keeps the hue
        // intact, where scaling the rgb bytes afterwards would drift it.
        const float scale = std::clamp(master * fixture.brightness, 0.0f, 1.0f);
        color.setBrightnessAlpha(color.getValFloat() * scale);

        Rgb8 rgb = hsvToRgb8(color);
        rgb.r = applyGamma(rgb.r, gamma);
        rgb.g = applyGamma(rgb.g, gamma);
        rgb.b = applyGamma(rgb.b, gamma);

        universe.setChannel(fixture.startChannel + fixture.offsetR, rgb.r);
        universe.setChannel(fixture.startChannel + fixture.offsetG, rgb.g);
        universe.setChannel(fixture.startChannel + fixture.offsetB, rgb.b);
    }
}

std::vector<std::string> FixtureMap::validate() const
{
    std::vector<std::string> warnings;

    // channel -> the fixture that already claimed it
    std::map<int, std::string> claimed;

    for (const Fixture& fixture : fixtures)
    {
        if (fixture.startChannel < 1)
        {
            warnings.push_back("fixture '" + fixture.name + "' has start_channel "
                             + std::to_string(fixture.startChannel) + "; DMX addresses start at 1");
            continue;
        }

        const int highest = fixtureHighestChannel(fixture);
        if (highest > DMX_CHANNEL_COUNT)
        {
            warnings.push_back("fixture '" + fixture.name + "' reaches channel " + std::to_string(highest)
                             + ", past the end of the universe (512); those channels will be dropped");
        }

        std::vector<int> used = {
            fixture.startChannel + fixture.offsetR,
            fixture.startChannel + fixture.offsetG,
            fixture.startChannel + fixture.offsetB,
        };
        if (fixture.dimmerChannel > 0)
        {
            used.push_back(fixture.dimmerChannel);
        }
        for (const auto& entry : fixture.staticChannels)
        {
            used.push_back(entry.first);
        }

        for (int channel : used)
        {
            auto it = claimed.find(channel);
            if (it != claimed.end() && it->second != fixture.name)
            {
                warnings.push_back("channel " + std::to_string(channel) + " is claimed by both '"
                                 + it->second + "' and '" + fixture.name + "'");
            }
            else
            {
                claimed[channel] = fixture.name;
            }
        }
    }

    return warnings;
}
