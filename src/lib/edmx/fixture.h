// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../ecore/hsv.h"

#include "universe.h"

///
/// Fixtures: the bridge between the library's HSV world and DMX channels.
/// A look thinks in HSV per node; an RGB fixture wants three bytes at an
/// address, possibly in another order, possibly behind a dimmer. The patch
/// (FixtureMap) carries that and renders colours into a ChannelBuffer.
///

namespace edmx
{
    struct Rgb8
    {
        uint8_t r{0};
        uint8_t g{0};
        uint8_t b{0};
    };

    /// HSV -> 8-bit RGB, what an RGB fixture expects.
    Rgb8 hsvToRgb8(const ecore::HSV& hsv);

    /// Perceptual correction for LEDs, which are linear in duty cycle and
    /// not to the eye. gamma <= 0 disables.
    uint8_t applyGamma(uint8_t value, float gamma);


    /* @brief One patched fixture: where its channels are and how it is wired.
    *
    * Build one from a FixtureProfile with instantiateProfile(), or fill the
    * fields and call resolveChannelOrder().
    */
    struct Fixture
    {
        std::string name;

        /// DMX address of the fixture's first channel, 1-based, as printed on
        /// the fixture's display.
        int startChannel{1};

        /// Channel order within the fixture, e.g. "rgb", "grb", "brg".
        std::string channelOrder{"rgb"};

        /// Absolute channel of a master dimmer, or 0 when the fixture has none.
        int dimmerChannel{0};
        uint8_t dimmerValue{255};

        /// Absolute channel -> fixed value, for strobe/mode/macro channels
        /// that must be parked for the fixture to behave.
        std::vector<std::pair<int, uint8_t>> staticChannels;

        /// Where this fixture sits in the rig, for spatial looks. Even
        /// spacing by index when a config omits it.
        float positionX{0.0f};
        float positionY{0.0f};
        bool hasPosition{false};

        /// Per-fixture trim, multiplied into the final value.
        float brightness{1.0f};

        /// Byte offsets from startChannel, resolved from channelOrder.
        int offsetR{0};
        int offsetG{1};
        int offsetB{2};
    };

    /// Fills offsetR/G/B from channelOrder. False on an order that is not a
    /// permutation of r, g and b.
    bool resolveChannelOrder(Fixture& fixture, std::string& outError);

    /// Highest channel the fixture touches, for overlap and range checks.
    int fixtureHighestChannel(const Fixture& fixture);


    /* @brief The channel layout of a fixture model, independent of where it
    * is patched: what the manual's channel chart says. Offsets are 1-based
    * within the fixture, as the chart numbers them.
    */
    struct FixtureProfile
    {
        std::string name;

        /// Channels the fixture occupies: the gap to the next one patched
        /// back to back.
        int footprint{3};

        int redOffset{1};
        int greenOffset{2};
        int blueOffset{3};

        /// Master dimmer, 0 when the model has none.
        int dimmerOffset{0};
        uint8_t dimmerValue{255};

        /// Offsets held at a fixed value so the fixture obeys its colour
        /// channels: strobe, mode, macro.
        std::vector<std::pair<int, uint8_t>> park;
    };

    /// One of the shipped profiles by name. False on an unknown name.
    bool lookupBuiltinProfile(const std::string& name, FixtureProfile& outProfile);

    /// Names of every shipped profile.
    std::vector<std::string> builtinProfileNames();

    /// A human-readable channel chart for a profile.
    std::string describeProfile(const FixtureProfile& profile);

    /// A profile at a DMX address, as a patched fixture. `address` is the
    /// number on the fixture's display.
    Fixture instantiateProfile(const FixtureProfile& profile,
                               const std::string& name,
                               int address);

    /// Checks a profile is consistent: offsets inside the footprint, no
    /// channel used twice. False with outError set.
    bool validateProfile(const FixtureProfile& profile, std::string& outError);


    /* @brief The patch: every fixture in the rig, in node order.
    *
    *     map.addFixture(instantiateProfile(profile, "par 1", 1));
    *     ...
    *     buffer.clear();
    *     map.render(colors, master, gamma, buffer);   // one HSV per fixture
    *     output.sendFrame(buffer, error);
    */
    class FixtureMap
    {
    public:
        void addFixture(const Fixture& fixture) { fixtures.push_back(fixture); }

        size_t size() const { return fixtures.size(); }
        const Fixture& operator[](size_t idx) const { return fixtures[idx]; }
        const std::vector<Fixture>& all() const { return fixtures; }

        /// Position of fixture `idx` along the rig, 0..1: the configured
        /// position, or even spacing by index.
        float normalizedPosition(size_t idx) const;

        /// Writes `colors` (one per fixture, index-aligned) into `buffer`
        /// with per-fixture trim, master brightness and gamma applied, plus
        /// each fixture's dimmer and parked channels. Missing colours go dark.
        void render(const std::vector<ecore::HSV>& colors,
                    float masterBrightness,
                    float gamma,
                    ChannelBuffer& buffer) const;

        /// Highest channel any fixture touches: what sizes the buffer.
        int highestChannel() const;

        /// Warnings for fixtures that overlap or run past `channelLimit`.
        /// Non-fatal: a rig mid-repatch should still light up.
        std::vector<std::string> validate(int channelLimit = DMX_CHANNEL_COUNT) const;

    private:
        std::vector<Fixture> fixtures;
    };
}
