// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>

#include "edmx/json.h"

#include "kits/palettes.h"

using namespace edmx;

namespace
{
    /// The built-ins from kits/palettes.h, exposed by name so a config can say
    /// "p_ritual" instead of restating four colours.
    struct NamedPalette
    {
        const char* name;
        const ecore::HSVPalette* palette;
    };

    const std::vector<NamedPalette>& namedPalettes()
    {
        static const std::vector<NamedPalette> palettes = {
            {"p_retrosunset",     &jpalettes::p_retrosunset},
            {"p_bluemagic",       &jpalettes::p_bluemagic},
            {"p_darkpurple_neo",  &jpalettes::p_darkpurple_neo},
            {"p_disney100",       &jpalettes::p_disney100},
            {"p_purplesky",       &jpalettes::p_purplesky},
            {"p_iceCream",        &jpalettes::p_iceCream},
            {"p_ritual",          &jpalettes::p_ritual},
            {"p_parrot",          &jpalettes::p_parrot},
            {"p_naturenight",     &jpalettes::p_naturenight},
            {"p_bootgradient",    &jpalettes::p_bootgradient},
            {"p_nostalgicrain",   &jpalettes::p_nostalgicrain},
        };
        return palettes;
    }

    /// rgb bytes -> the library's HSV. The config speaks hex because that is
    /// what a lighting designer copies out of a colour picker; everything
    /// downstream stays in HSV.
    ecore::HSV rgbToHsv(int r, int g, int b)
    {
        const float rf = static_cast<float>(r) / 255.0f;
        const float gf = static_cast<float>(g) / 255.0f;
        const float bf = static_cast<float>(b) / 255.0f;

        const float maxComponent = std::max(rf, std::max(gf, bf));
        const float minComponent = std::min(rf, std::min(gf, bf));
        const float delta = maxComponent - minComponent;

        float hue = 0.0f;
        if (delta > 0.00001f)
        {
            if (maxComponent == rf)
            {
                hue = 60.0f * std::fmod((gf - bf) / delta, 6.0f);
            }
            else if (maxComponent == gf)
            {
                hue = 60.0f * (((bf - rf) / delta) + 2.0f);
            }
            else
            {
                hue = 60.0f * (((rf - gf) / delta) + 4.0f);
            }
        }
        if (hue < 0.0f)
        {
            hue += 360.0f;
        }

        const float saturation = (maxComponent <= 0.00001f) ? 0.0f : (delta / maxComponent);
        return ecore::HSV(hue, saturation, maxComponent);
    }

    bool hexDigit(char c, int& outValue)
    {
        if (c >= '0' && c <= '9') { outValue = c - '0'; return true; }
        if (c >= 'a' && c <= 'f') { outValue = c - 'a' + 10; return true; }
        if (c >= 'A' && c <= 'F') { outValue = c - 'A' + 10; return true; }
        return false;
    }

    /// Reads a colour from either a hex string or an {h,s,v} object.
    bool readColor(const JsonValue& value, ecore::HSV& outColor)
    {
        if (value.getType() == JsonValue::Type::String)
        {
            return parseColorString(value.asString(), outColor);
        }

        if (value.isObject())
        {
            const JsonValue& h = value["h"];
            const JsonValue& s = value["s"];
            const JsonValue& v = value["v"];
            if (h.isNull() && s.isNull() && v.isNull())
            {
                return false;
            }
            outColor = ecore::HSV(h.asFloat(0.0f), s.asFloat(1.0f), v.asFloat(1.0f));
            return true;
        }

        return false;
    }
}

bool edmx::parseColorString(const std::string& text, ecore::HSV& outColor)
{
    std::string hex = text;
    if (!hex.empty() && hex[0] == '#')
    {
        hex.erase(hex.begin());
    }

    // #rgb shorthand, expanded the way css does it
    if (hex.size() == 3)
    {
        hex = std::string{hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    }

    if (hex.size() != 6)
    {
        return false;
    }

    int components[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i)
    {
        int high = 0;
        int low = 0;
        if (!hexDigit(hex[static_cast<size_t>(i * 2)], high) ||
            !hexDigit(hex[static_cast<size_t>(i * 2 + 1)], low))
        {
            return false;
        }
        components[i] = (high << 4) | low;
    }

    outColor = rgbToHsv(components[0], components[1], components[2]);
    return true;
}

bool edmx::lookupNamedPalette(const std::string& name, ecore::HSVPalette& outPalette)
{
    for (const NamedPalette& entry : namedPalettes())
    {
        if (entry.name == name)
        {
            outPalette = *entry.palette;
            return true;
        }
    }
    return false;
}

std::vector<std::string> edmx::builtinPaletteNames()
{
    std::vector<std::string> names;
    for (const NamedPalette& entry : namedPalettes())
    {
        names.push_back(entry.name);
    }
    return names;
}

bool edmx::loadConfig(const std::string& path, Config& outConfig, std::string& outError)
{
    JsonValue root;
    if (!parseJsonFile(path, root, outError))
    {
        return false;
    }

    if (!root.isObject())
    {
        outError = path + ": top level must be a JSON object";
        return false;
    }

    Config config;

    // ---- device -------------------------------------------------------
    {
        const JsonValue& device = root["device"];
        config.device.type            = device["type"].asString(config.device.type);
        config.device.port            = device["port"].asString(config.device.port);
        config.device.baud            = device["baud"].asInt(config.device.baud);
        config.device.fps             = device["fps"].asFloat(config.device.fps);
        config.device.consoleChannels = device["console_channels"].asInt(config.device.consoleChannels);

        if (config.device.fps <= 0.0f || config.device.fps > 200.0f)
        {
            config.warnings.push_back("device.fps of " + std::to_string(config.device.fps)
                                    + " is out of range; falling back to 40");
            config.device.fps = 40.0f;
        }
    }

    // ---- master -------------------------------------------------------
    {
        const JsonValue& master = root["master"];
        config.master.brightness = master["brightness"].asFloat(config.master.brightness);
        config.master.gamma      = master["gamma"].asFloat(config.master.gamma);
        config.master.brightness = std::clamp(config.master.brightness, 0.0f, 1.0f);
    }

    // ---- pattern ------------------------------------------------------
    {
        const JsonValue& pattern = root["pattern"];
        config.pattern.name       = pattern["name"].asString(config.pattern.name);
        config.pattern.speed      = pattern["speed"].asFloat(config.pattern.speed);
        config.pattern.width      = pattern["width"].asFloat(config.pattern.width);
        config.pattern.brightness = std::clamp(pattern["brightness"].asFloat(config.pattern.brightness), 0.0f, 1.0f);

        if (!pattern["color"].isNull())
        {
            if (!readColor(pattern["color"], config.pattern.solidColor))
            {
                config.warnings.push_back("pattern.color could not be read; using the default red");
            }
        }

        const JsonValue& palette = pattern["palette"];
        if (palette.getType() == JsonValue::Type::String)
        {
            config.pattern.paletteName = palette.asString();
        }
        else if (palette.isArray())
        {
            for (size_t i = 0; i < palette.size(); ++i)
            {
                ecore::HSV stop;
                if (readColor(palette[i], stop))
                {
                    config.pattern.palette.push_back(stop);
                }
                else
                {
                    config.warnings.push_back("pattern.palette entry " + std::to_string(i)
                                            + " could not be read; skipping it");
                }
            }
            if (config.pattern.palette.size() < 2)
            {
                config.warnings.push_back("pattern.palette needs at least 2 usable colors; "
                                          "falling back to the named palette");
                config.pattern.palette.clear();
            }
        }

        if (config.pattern.width <= 0.0f)
        {
            config.warnings.push_back("pattern.width must be positive; using 1.0");
            config.pattern.width = 1.0f;
        }
    }

    // ---- profiles -----------------------------------------------------
    // User-defined models, on top of the shipped ones. Declaring the layout
    // once here is what lets the fixtures array stay one line per bank.
    std::vector<FixtureProfile> customProfiles;
    {
        const JsonValue& profiles = root["profiles"];
        if (profiles.isObject())
        {
            for (const std::string& name : profiles.keys())
            {
                const JsonValue& entry = profiles[name];

                FixtureProfile profile;
                profile.name         = name;
                profile.footprint    = entry["footprint"].asInt(3);
                profile.redOffset    = entry["red"].asInt(1);
                profile.greenOffset  = entry["green"].asInt(2);
                profile.blueOffset   = entry["blue"].asInt(3);
                profile.dimmerOffset = entry["dimmer"].asInt(0);
                profile.dimmerValue  = static_cast<uint8_t>(std::clamp(entry["dimmer_value"].asInt(255), 0, 255));

                const JsonValue& park = entry["park"];
                if (park.isObject())
                {
                    for (const std::string& key : park.keys())
                    {
                        try
                        {
                            const int offset = std::stoi(key);
                            const int value = std::clamp(park[key].asInt(0), 0, 255);
                            profile.park.emplace_back(offset, static_cast<uint8_t>(value));
                        }
                        catch (const std::exception&)
                        {
                            config.warnings.push_back("profile '" + name + "': park key '" + key
                                                    + "' is not a channel offset; skipping it");
                        }
                    }
                }

                std::string profileError;
                if (!validateProfile(profile, profileError))
                {
                    outError = path + ": " + profileError;
                    return false;
                }

                customProfiles.push_back(profile);
            }
        }
    }

    const auto findProfile = [&](const std::string& name, FixtureProfile& out) -> bool {
        // a config's own profiles shadow the shipped ones, so a rig can
        // correct a built-in without us having to ship a fix
        for (const FixtureProfile& profile : customProfiles)
        {
            if (profile.name == name)
            {
                out = profile;
                return true;
            }
        }
        return lookupBuiltinProfile(name, out);
    };

    // ---- fixtures -----------------------------------------------------
    {
        const JsonValue& fixtures = root["fixtures"];
        if (!fixtures.isArray() || fixtures.size() == 0)
        {
            outError = path + ": needs a non-empty \"fixtures\" array; there is nothing to light otherwise";
            return false;
        }

        for (size_t i = 0; i < fixtures.size(); ++i)
        {
            const JsonValue& entry = fixtures[i];

            const std::string profileName = entry["profile"].asString();

            // ---- profile-driven: one entry patches a whole bank ----------
            if (!profileName.empty())
            {
                FixtureProfile profile;
                if (!findProfile(profileName, profile))
                {
                    std::string known;
                    for (const std::string& candidate : builtinProfileNames())
                    {
                        known += (known.empty() ? "" : ", ") + candidate;
                    }
                    outError = path + ": unknown profile '" + profileName
                             + "' (built-ins: " + known + "; or define it in \"profiles\")";
                    return false;
                }

                // With a profile, the address is the fixture's own DMX address,
                // the number set on its display. start_channel is accepted as a
                // synonym because that is what people type.
                int address = entry["address"].asInt(entry["start_channel"].asInt(-1));
                if (address < 0)
                {
                    outError = path + ": fixture entry " + std::to_string(i)
                             + " uses profile '" + profileName + "' but has no \"address\"";
                    return false;
                }

                const int count = std::max(1, entry["count"].asInt(1));
                // fixtures patched back to back sit one footprint apart, which
                // is the overwhelmingly common case; spacing overrides it
                const int spacing = entry["spacing"].asInt(profile.footprint);
                const std::string prefix = entry["name"].asString(profileName);
                const float trim = std::clamp(entry["brightness"].asFloat(1.0f), 0.0f, 1.0f);

                const JsonValue& position = entry["position"];
                const bool explicitPosition = position.isArray() && position.size() >= 1;

                for (int index = 0; index < count; ++index)
                {
                    const std::string name = (count == 1)
                        ? prefix
                        : (prefix + "_" + std::to_string(index + 1));

                    Fixture fixture = instantiateProfile(profile, name, address + (index * spacing));
                    fixture.brightness = trim;

                    if (explicitPosition)
                    {
                        // a single stated position applies to the whole bank
                        fixture.positionX = position[0].asFloat(0.0f);
                        fixture.positionY = (position.size() >= 2) ? position[1].asFloat(0.0f) : 0.0f;
                        fixture.hasPosition = true;
                    }
                    else if (count > 1)
                    {
                        // otherwise spread the bank evenly, so spatial patterns
                        // sweep along it in patch order
                        fixture.positionX = static_cast<float>(index) / static_cast<float>(count - 1);
                        fixture.positionY = 0.0f;
                        fixture.hasPosition = true;
                    }

                    config.fixtures.addFixture(fixture);
                }

                continue;
            }

            // ---- explicit: every channel spelled out ---------------------
            Fixture fixture;
            fixture.name         = entry["name"].asString("fixture_" + std::to_string(i));
            fixture.startChannel = entry["start_channel"].asInt(static_cast<int>(i) * 3 + 1);
            fixture.channelOrder = entry["channels"].asString("rgb");
            fixture.dimmerChannel = entry["dimmer_channel"].asInt(0);
            fixture.dimmerValue  = static_cast<uint8_t>(std::clamp(entry["dimmer_value"].asInt(255), 0, 255));
            fixture.brightness   = std::clamp(entry["brightness"].asFloat(1.0f), 0.0f, 1.0f);

            std::string orderError;
            if (!resolveChannelOrder(fixture, orderError))
            {
                outError = path + ": " + orderError;
                return false;
            }

            const JsonValue& position = entry["position"];
            if (position.isArray() && position.size() >= 1)
            {
                fixture.positionX = position[0].asFloat(0.0f);
                fixture.positionY = (position.size() >= 2) ? position[1].asFloat(0.0f) : 0.0f;
                fixture.hasPosition = true;
            }

            // static_channels is an object because JSON has no integer keys:
            // {"7": 255} parks channel 7 at full.
            const JsonValue& statics = entry["static_channels"];
            if (statics.isObject())
            {
                for (const std::string& key : statics.keys())
                {
                    try
                    {
                        const int channel = std::stoi(key);
                        const int value = std::clamp(statics[key].asInt(0), 0, 255);
                        fixture.staticChannels.emplace_back(channel, static_cast<uint8_t>(value));
                    }
                    catch (const std::exception&)
                    {
                        config.warnings.push_back("fixture '" + fixture.name + "': static_channels key '"
                                                + key + "' is not a channel number; skipping it");
                    }
                }
            }

            config.fixtures.addFixture(fixture);
        }
    }

    for (const std::string& warning : config.fixtures.validate())
    {
        config.warnings.push_back(warning);
    }

    outConfig = config;
    return true;
}
