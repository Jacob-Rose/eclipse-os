// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/config.h"

#include <algorithm>
#include <fstream>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

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

namespace
{
    /// Finds the file behind a `"device": "obelisk"` reference.
    ///
    /// Looked for beside the environment first and then in `devices/` next to
    /// it, so a show can keep a one-off rig in its own folder while the
    /// sculptures that appear in every show live together. A reference may also
    /// be a path, with or without the .json, because sooner or later someone
    /// writes one.
    bool resolveDevicePath(const std::string& environmentPath,
                           const std::string& reference,
                           std::string& outPath)
    {
        std::string directory;
        const size_t slash = environmentPath.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            directory = environmentPath.substr(0, slash + 1);
        }

        const bool named = reference.find('/') == std::string::npos
                        && reference.find('\\') == std::string::npos;
        const std::string withSuffix =
            (reference.size() > 5 && reference.substr(reference.size() - 5) == ".json")
                ? reference : (reference + ".json");

        std::vector<std::string> candidates;
        if (named)
        {
            candidates.push_back(directory + "devices/" + withSuffix);
            candidates.push_back(directory + "../devices/" + withSuffix);
        }
        candidates.push_back(directory + withSuffix);
        candidates.push_back(withSuffix);

        for (const std::string& candidate : candidates)
        {
            std::ifstream probe(candidate);
            if (probe.good())
            {
                outPath = candidate;
                return true;
            }
        }
        return false;
    }

    /// How many channels a device's frame buffer actually has.
    ///
    /// A DMX rig cannot reach past its universe; a pixel rig's buffer is as
    /// long as its patch. Which one this is follows from the output, not from
    /// the patch: the obelisk's 1032 channels are only legal because nothing is
    /// putting them on a DMX wire.
    int channelLimitFor(const Device& device)
    {
        const bool onDmxWire = device.output.type == "enttec_pro"
                            || device.output.type == "enttec_open";
        return onDmxWire ? DMX_CHANNEL_COUNT
                         : std::max(device.fixtures.highestChannel(), DMX_CHANNEL_COUNT);
    }

    /// Reads one device out of a JSON object: how its numbers are read, where
    /// its frames go, and what its fixtures are.
    ///
    /// Split out from loadConfig because a device is now a thing in its own
    /// file, and an environment parses several of them. `root` is the object
    /// holding the device's keys - the whole file for a device definition, or
    /// the whole file again for a legacy single-rig config, which is the same
    /// shape by construction.
    bool parseDevice(const JsonValue& root,
                     const std::string& path,
                     Device& device,
                     std::vector<std::string>& warnings,
                     std::string& outError)
    {
    // ---- addressing ---------------------------------------------------
    // Read first: every channel number below is interpreted through it.
    int addressBias = 0;
    {
        const std::string mode = root["addressing"].asString("one");
        if (mode == "zero" || mode == "zero-based" || mode == "0")
        {
            device.addressing = Addressing::ZeroBased;
            // a config address of 0 is DMX slot 1
            addressBias = 1;
        }
        else if (mode == "one" || mode == "one-based" || mode == "1")
        {
            device.addressing = Addressing::OneBased;
        }
        else
        {
            outError = path + ": addressing must be \"zero\" or \"one\", got '" + mode + "'";
            return false;
        }
    }

    // ---- coordinate space ---------------------------------------------
    // Same idea as addressing, for the other set of numbers in the file:
    // it says what a fixture's "position" means, and nothing else.
    {
        const std::string space = root["coord_space"].asString("normalized");
        if (space == "literal" || space == "pattern")
        {
            device.coordSpace = CoordSpace::Literal;
        }
        else if (space == "normalized" || space == "normalised" || space == "rig")
        {
            device.coordSpace = CoordSpace::Normalized;
        }
        else
        {
            outError = path + ": coord_space must be \"normalized\" or \"literal\", got '" + space + "'";
            return false;
        }
    }

    // ---- device -------------------------------------------------------
    {
        // `output` is the name now; `device` is still read so every config written
        // before environments existed keeps working.
        const JsonValue& output = root["output"].isObject() ? root["output"] : root["device"];
        device.output.type            = output["type"].asString(device.output.type);
        device.output.port            = output["port"].asString(device.output.port);
        device.output.baud            = output["baud"].asInt(device.output.baud);
        device.output.fps             = output["fps"].asFloat(device.output.fps);
        device.output.consoleChannels = output["console_channels"].asInt(device.output.consoleChannels);

        if (device.output.fps <= 0.0f || device.output.fps > 200.0f)
        {
            warnings.push_back("device.fps of " + std::to_string(device.output.fps)
                                    + " is out of range; falling back to 40");
            device.output.fps = 40.0f;
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
                            warnings.push_back("profile '" + name + "': park key '" + key
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
                // sentinel below any legal address in either numbering
                const int missing = -1000;
                int address = entry["address"].asInt(entry["start_channel"].asInt(missing));
                if (address == missing)
                {
                    outError = path + ": fixture entry " + std::to_string(i)
                             + " uses profile '" + profileName + "' but has no \"address\"";
                    return false;
                }
                address += addressBias;

                const int count = std::max(1, entry["count"].asInt(1));
                // fixtures patched back to back sit one footprint apart, which
                // is the overwhelmingly common case; spacing overrides it
                const int spacing = entry["spacing"].asInt(profile.footprint);
                const std::string prefix = entry["name"].asString(profileName);
                const float trim = std::clamp(entry["brightness"].asFloat(1.0f), 0.0f, 1.0f);

                const JsonValue& position = entry["position"];
                const bool explicitPosition = position.isArray() && position.size() >= 1;

                // How far the bank moves per fixture. This is what lets one
                // entry describe a *run* rather than a point, which is the
                // whole of a relic's geometry: the obelisk's eight vertical
                // strips are eight of these, and they read alongside the eight
                // GenerateAxisRow calls in obelisk.cpp that build the same
                // thing on the sculpture. Absent, the bank sits where it is
                // stated and the old behaviour stands.
                const JsonValue& positionStep = entry["position_step"];
                const bool hasStep = positionStep.isArray() && positionStep.size() >= 1;
                const float stepX = hasStep ? positionStep[0].asFloat(0.0f) : 0.0f;
                const float stepY = (hasStep && positionStep.size() >= 2) ? positionStep[1].asFloat(0.0f) : 0.0f;

                if (hasStep && !explicitPosition)
                {
                    warnings.push_back("fixture entry " + std::to_string(i)
                                            + " has \"position_step\" but no \"position\" to step from;"
                                              " starting the run at the origin");
                }

                for (int index = 0; index < count; ++index)
                {
                    const std::string name = (count == 1)
                        ? prefix
                        : (prefix + "_" + std::to_string(index + 1));

                    Fixture fixture = instantiateProfile(profile, name, address + (index * spacing));
                    fixture.brightness = trim;

                    if (explicitPosition || hasStep)
                    {
                        // a stated position is where the bank starts; without a
                        // step the whole bank stays there, as it always has
                        const float baseX = explicitPosition ? position[0].asFloat(0.0f) : 0.0f;
                        const float baseY = (explicitPosition && position.size() >= 2)
                            ? position[1].asFloat(0.0f) : 0.0f;

                        fixture.positionX = baseX + stepX * static_cast<float>(index);
                        fixture.positionY = baseY + stepY * static_cast<float>(index);
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

                    device.fixtures.addFixture(fixture);
                }

                continue;
            }

            // ---- explicit: every channel spelled out ---------------------
            Fixture fixture;
            fixture.name         = entry["name"].asString("fixture_" + std::to_string(i));
            fixture.startChannel = entry["start_channel"].asInt(static_cast<int>(i) * 3 + 1 - addressBias)
                                 + addressBias;
            fixture.channelOrder = entry["channels"].asString("rgb");

            // An *absent* dimmer_channel means the fixture has no dimmer, in
            // either numbering, so it stays 0 instead of being biased into
            // channel 1. An explicit 0 under zero-based addressing is a real
            // dimmer sitting in the first slot, and is shifted like any other.
            const int dimmer = entry["dimmer_channel"].asInt(-1000);
            fixture.dimmerChannel = (dimmer == -1000) ? 0 : (dimmer + addressBias);
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
                        const int channel = std::stoi(key) + addressBias;
                        const int value = std::clamp(statics[key].asInt(0), 0, 255);
                        fixture.staticChannels.emplace_back(channel, static_cast<uint8_t>(value));
                    }
                    catch (const std::exception&)
                    {
                        warnings.push_back("fixture '" + fixture.name + "': static_channels key '"
                                                + key + "' is not a channel number; skipping it");
                    }
                }
            }

            device.fixtures.addFixture(fixture);
        }
    }

        return true;
    }
}

bool edmx::loadDevice(const std::string& path, Device& outDevice, std::string& outError,
                      std::vector<std::string>* outWarnings)
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

    // A device file may wrap its contents in a "device" object or not. Both
    // read the same; the wrapper only exists because it makes a file that is
    // obviously a device rather than obviously anything else.
    const JsonValue& body = root["device"].isObject() ? root["device"] : root;

    Device device;
    device.source = path;
    device.name = body["name"].asString(root["name"].asString(""));

    std::vector<std::string> warnings;
    if (!parseDevice(body, path, device, warnings, outError))
    {
        return false;
    }

    if (outWarnings)
    {
        outWarnings->insert(outWarnings->end(), warnings.begin(), warnings.end());
    }

    outDevice = device;
    return true;
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

    // ---- master -------------------------------------------------------
    {
        const JsonValue& master = root["master"];
        config.master.brightness = master["brightness"].asFloat(config.master.brightness);
        config.master.gamma      = master["gamma"].asFloat(config.master.gamma);
        config.master.brightness = std::clamp(config.master.brightness, 0.0f, 1.0f);
    }

    // ---- midi ---------------------------------------------------------
    {
        const JsonValue& midi = root["midi"];

        // Naming a port is the whole point of the block, so it also counts as
        // asking for it. Someone who writes `"midi": {"port": "loopMIDI"}` and
        // gets silence because they left `enabled` out has been failed by us,
        // not by their config.
        const bool named = !midi["port"].isNull();
        config.midi.enabled      = midi["enabled"].asBool(named);
        config.midi.port         = midi["port"].asString(config.midi.port);
        config.midi.followClock  = midi["clock"].asBool(config.midi.followClock);
        config.midi.followNotes  = midi["notes"].asBool(config.midi.followNotes);
        config.midi.beatNote     = midi["beat_note"].asInt(config.midi.beatNote);
        config.midi.bpmNote      = midi["bpm_note"].asInt(config.midi.bpmNote);
        config.midi.vuInstantNote = midi["vu_instant_note"].asInt(config.midi.vuInstantNote);
        config.midi.vuAverageNote = midi["vu_average_note"].asInt(config.midi.vuAverageNote);
        config.midi.vuMeterNote   = midi["vu_meter_note"].asInt(config.midi.vuMeterNote);
        config.midi.beatChannel  = midi["beat_channel"].asInt(config.midi.beatChannel);
        config.midi.bpm          = midi["bpm"].asFloat(config.midi.bpm);
        config.midi.freeRun      = midi["free_run"].asBool(config.midi.freeRun);

        const JsonValue& ignore = midi["ignore"];
        if (ignore.isArray())
        {
            for (size_t i = 0; i < ignore.size(); ++i)
            {
                const std::string fragment = ignore[i].asString();
                if (!fragment.empty())
                {
                    config.midi.ignore.push_back(fragment);
                }
            }
        }
        else if (ignore.getType() == JsonValue::Type::String)
        {
            // One name is the common case; not making people write a list for
            // it is worth four lines here.
            config.midi.ignore.push_back(ignore.asString());
        }

        if (config.midi.bpm < 30.0f || config.midi.bpm > 300.0f)
        {
            config.warnings.push_back("midi.bpm of " + std::to_string(config.midi.bpm)
                                    + " is not a tempo; falling back to 128");
            config.midi.bpm = 128.0f;
        }

        if (config.midi.beatChannel != -1 &&
            (config.midi.beatChannel < 1 || config.midi.beatChannel > 16))
        {
            config.warnings.push_back("midi.beat_channel must be 1-16 or -1 for any; using any");
            config.midi.beatChannel = -1;
        }

        const auto checkNote = [&config](const char* key, int& note, int fallback) {
            if (note != -1 && (note < 0 || note > 127))
            {
                config.warnings.push_back(std::string(key) + " must be 0-127, or -1 to switch it "
                                          "off; using " + std::to_string(fallback));
                note = fallback;
            }
        };
        checkNote("midi.beat_note", config.midi.beatNote, 50);
        checkNote("midi.bpm_note", config.midi.bpmNote, 52);
        checkNote("midi.vu_instant_note", config.midi.vuInstantNote, 64);
        checkNote("midi.vu_average_note", config.midi.vuAverageNote, 68);
        checkNote("midi.vu_meter_note", config.midi.vuMeterNote, 69);

        if (config.midi.beatNote != -1 && config.midi.beatNote == config.midi.bpmNote)
        {
            config.warnings.push_back("midi.beat_note and midi.bpm_note are the same note, so the "
                                      "tempo message would be taken as a beat; ignoring bpm_note");
            config.midi.bpmNote = -1;
        }

        // A meter sharing the beat's note would fire a beat dozens of times a
        // second, so the meter loses rather than the beat.
        const std::pair<const char*, int*> meters[] = {
            {"midi.vu_instant_note", &config.midi.vuInstantNote},
            {"midi.vu_average_note", &config.midi.vuAverageNote},
            {"midi.vu_meter_note",   &config.midi.vuMeterNote},
        };
        for (const auto& entry : meters)
        {
            if (config.midi.beatNote != -1 && config.midi.beatNote == *entry.second)
            {
                config.warnings.push_back(std::string(entry.first) + " is the same note as "
                                          "midi.beat_note, so the meter would be taken as a beat; "
                                          "ignoring it");
                *entry.second = -1;
            }
        }

        if (config.midi.enabled && !config.midi.followClock && !config.midi.followNotes)
        {
            config.warnings.push_back("midi is enabled but both clock and notes are off, "
                                      "so nothing will ever set the tempo");
        }
    }

    // ---- pattern ------------------------------------------------------
    {
        const JsonValue& pattern = root["pattern"];
        config.pattern.name       = pattern["name"].asString(config.pattern.name);
        config.pattern.speed      = pattern["speed"].asFloat(config.pattern.speed);
        config.pattern.width      = pattern["width"].asFloat(config.pattern.width);
        config.pattern.brightness = std::clamp(pattern["brightness"].asFloat(config.pattern.brightness), 0.0f, 1.0f);
        config.pattern.stateName = pattern["state"].asString(config.pattern.stateName);

        // Only override what the file actually says. A pattern's own frame is
        // the right default, so an absent key must stay absent rather than
        // resolving to some number we made up here.
        auto readOptional = [&pattern](const char* key, std::optional<float>& out) {
            if (!pattern[key].isNull())
            {
                out = pattern[key].asFloat(0.0f);
            }
        };
        readOptional("coord_origin_x", config.pattern.coordOriginX);
        readOptional("coord_origin_y", config.pattern.coordOriginY);
        readOptional("coord_span_x",   config.pattern.coordSpanX);
        readOptional("coord_span_y",   config.pattern.coordSpanY);

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

    // ---- devices ------------------------------------------------------
    //
    // Which shape this file is depends on one thing: whether it has a
    // `devices` array. With one it is an environment and the devices are
    // named elsewhere; without one it is a single rig described in place,
    // which is every config written before environments existed.
    const JsonValue& devices = root["devices"];

    if (devices.isArray() && devices.size() > 0)
    {
        for (size_t i = 0; i < devices.size(); ++i)
        {
            const JsonValue& entry = devices[i];

            const std::string reference = entry["device"].asString(entry["use"].asString(""));

            Device device;
            std::vector<std::string> deviceWarnings;

            if (!reference.empty())
            {
                // By name: a device described once, in its own file, and
                // referred to from every show it appears in.
                std::string devicePath;
                if (!resolveDevicePath(path, reference, devicePath))
                {
                    outError = path + ": cannot find device '" + reference + "'"
                               " (looked beside this file and in devices/)";
                    return false;
                }

                if (!loadDevice(devicePath, device, outError, &deviceWarnings))
                {
                    return false;
                }
            }
            else if (entry["fixtures"].isArray() || entry["device"].isObject())
            {
                // Spelled out in place. Round-tripping an environment writes
                // this form, because a config that came back out referring to
                // files it did not carry would not be the same config.
                const JsonValue& body = entry["device"].isObject() ? entry["device"] : entry;
                device.source = path;
                device.name = body["name"].asString("device_" + std::to_string(i));

                if (!parseDevice(body, path, device, deviceWarnings, outError))
                {
                    return false;
                }
            }
            else
            {
                outError = path + ": devices entry " + std::to_string(i)
                         + " has neither a \"device\" naming one nor \"fixtures\" spelling one out";
                return false;
            }

            // An entry may name the instance, so two of the same sculpture in
            // one room are tellable apart.
            device.name = entry["name"].asString(
                device.name.empty() ? (reference.empty() ? ("device_" + std::to_string(i)) : reference)
                                    : device.name);

            // ---- placement ---------------------------------------------
            const JsonValue& offset = entry["offset"];
            if (offset.isArray() && offset.size() >= 1)
            {
                device.placement.offsetX = offset[0].asFloat(0.0f);
                device.placement.offsetY = (offset.size() >= 2) ? offset[1].asFloat(0.0f) : 0.0f;
            }

            const JsonValue& scale = entry["scale"];
            if (scale.isArray() && scale.size() >= 1)
            {
                device.placement.scaleX = scale[0].asFloat(1.0f);
                device.placement.scaleY = (scale.size() >= 2) ? scale[1].asFloat(1.0f)
                                                             : device.placement.scaleX;
            }
            else if (scale.isNumber())
            {
                // A bare number scales both axes, which is what "make this
                // half the size" almost always means.
                device.placement.scaleX = scale.asFloat(1.0f);
                device.placement.scaleY = device.placement.scaleX;
            }

            // null on an axis means "leave this one alone", which is the case
            // that matters: mythos26 only reads y, so the obelisk wants its y
            // fitted to the show's 0..1 while its x keeps its own 0..7 - the
            // units obelisk_seasons reads to pick a palette per side. Fit both
            // and a relic look goes flat.
            const JsonValue& fit = entry["fit"];
            if (fit.isArray() && fit.size() >= 1)
            {
                if (!fit[0].isNull())
                {
                    device.placement.fitWidth = fit[0].asFloat(1.0f);
                }
                if (fit.size() >= 2 && !fit[1].isNull())
                {
                    device.placement.fitHeight = fit[1].asFloat(1.0f);
                }
            }
            else if (fit.isNumber())
            {
                device.placement.fitWidth = fit.asFloat(1.0f);
                device.placement.fitHeight = *device.placement.fitWidth;
            }

            // A view property, not a mapping one - the panel's size on screen,
            // and nothing the pattern ever sees. Read here so the schema has
            // one definition; the viewer is what acts on it.
            const JsonValue& view = entry["view_scale"].isNull()
                ? entry["view"]["scale"] : entry["view_scale"];
            if (view.isArray() && view.size() >= 1)
            {
                device.placement.viewScaleX = view[0].asFloat(1.0f);
                device.placement.viewScaleY = (view.size() >= 2) ? view[1].asFloat(1.0f)
                                                                 : device.placement.viewScaleX;
            }
            else if (view.isNumber())
            {
                device.placement.viewScaleX = view.asFloat(1.0f);
                device.placement.viewScaleY = device.placement.viewScaleX;
            }

            device.brightness = std::clamp(entry["brightness"].asFloat(1.0f), 0.0f, 1.0f);

            // ---- overrides ---------------------------------------------
            // A device file says what a sculpture *is*; where it is plugged in
            // this week belongs to the room, not to the sculpture.
            const JsonValue& output = entry["output"].isObject() ? entry["output"] : entry["device_output"];
            if (output.isObject())
            {
                device.output.type            = output["type"].asString(device.output.type);
                device.output.port            = output["port"].asString(device.output.port);
                device.output.baud            = output["baud"].asInt(device.output.baud);
                device.output.fps             = output["fps"].asFloat(device.output.fps);
                device.output.consoleChannels = output["console_channels"].asInt(device.output.consoleChannels);
            }
            if (entry["port"].isString())
            {
                device.output.port = entry["port"].asString(device.output.port);
            }

            const std::string space = entry["coord_space"].asString("");
            if (space == "literal" || space == "pattern")
            {
                device.coordSpace = CoordSpace::Literal;
            }
            else if (space == "normalized" || space == "normalised" || space == "rig")
            {
                device.coordSpace = CoordSpace::Normalized;
            }
            else if (!space.empty())
            {
                outError = path + ": devices entry " + std::to_string(i)
                         + " coord_space must be \"normalized\" or \"literal\", got '" + space + "'";
                return false;
            }

            for (const std::string& warning : deviceWarnings)
            {
                config.warnings.push_back("[" + device.name + "] " + warning);
            }

            config.devices.push_back(std::move(device));
        }
    }
    else
    {
        // One rig, described in place. The environment is implicit and holds a
        // single device sitting at the origin.
        Device device;
        device.source = path;
        device.name = root["name"].asString("rig");

        if (!parseDevice(root, path, device, config.warnings, outError))
        {
            return false;
        }

        config.devices.push_back(std::move(device));
    }

    for (Device& device : config.devices)
    {
        for (const std::string& warning : device.fixtures.validate(channelLimitFor(device)))
        {
            config.warnings.push_back("[" + device.name + "] " + warning);
        }
    }

    outConfig = config;
    return true;
}

size_t Config::fixtureCount() const
{
    size_t total = 0;
    for (const Device& device : devices)
    {
        total += device.fixtures.size();
    }
    return total;
}

size_t Config::firstFixtureOf(size_t index) const
{
    size_t at = 0;
    for (size_t i = 0; i < index && i < devices.size(); ++i)
    {
        at += devices[i].fixtures.size();
    }
    return at;
}
