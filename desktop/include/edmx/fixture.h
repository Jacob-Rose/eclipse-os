// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// eclipse-os library, reached through the repo's src/ root (see CMakeLists)
#include "lib/ecore/hsv.h"

#include "edmx/dmx_output.h"

///
/// Fixture mapping: the bridge between the library's HSV world and DMX channels.
///
/// eclipse-os thinks in HSV per node. A DMX RGB fixture wants three bytes at
/// some address, possibly in a non-rgb order, possibly behind a master dimmer.
/// This is where that translation lives, driven entirely by the config file.
///

namespace edmx
{
    struct Rgb8
    {
        uint8_t r{0};
        uint8_t g{0};
        uint8_t b{0};
    };

    /// HSV -> 8-bit RGB. Takes the library's HSV, whose h/s/v are fixed-point
    /// under the hood, and produces what a DMX RGB fixture expects.
    Rgb8 hsvToRgb8(const ecore::HSV& hsv);

    /// Perceptual correction. LEDs are linear in duty cycle and eyes are not,
    /// so without this a fade spends most of its travel looking "already on".
    /// gamma <= 0 disables.
    uint8_t applyGamma(uint8_t value, float gamma);


    /// One patched fixture.
    struct Fixture
    {
        std::string name;

        /// DMX address of the fixture's first channel, 1-based, exactly as
        /// printed on the fixture's own display.
        int startChannel{1};

        /// Channel order within the fixture, e.g. "rgb", "grb", "brg". Offsets
        /// are derived from this, so a GRB fixture needs no other config.
        std::string channelOrder{"rgb"};

        /// Absolute channel of a master dimmer, or 0 when the fixture has none.
        /// Many cheap RGB pars will output nothing until this is raised.
        int dimmerChannel{0};
        uint8_t dimmerValue{255};

        /// Absolute channel -> fixed value. For strobe/mode/macro channels that
        /// must be parked at a specific value for the fixture to behave.
        std::vector<std::pair<int, uint8_t>> staticChannels;

        /// Where this fixture sits in the rig, used by spatial patterns. When
        /// the config omits it we fall back to even spacing by index.
        float positionX{0.0f};
        float positionY{0.0f};
        bool hasPosition{false};

        /// Per-fixture trim, multiplied into the final value.
        float brightness{1.0f};

        /// Resolved from channelOrder at load: byte offsets from startChannel.
        int offsetR{0};
        int offsetG{1};
        int offsetB{2};
    };

    /// Fills offsetR/G/B from channelOrder. Returns false on an order string
    /// that is not a permutation of r, g and b.
    bool resolveChannelOrder(Fixture& fixture, std::string& outError);

    /// Highest channel the fixture touches, for overlap and range checks.
    int fixtureHighestChannel(const Fixture& fixture);


    /// The patch: every fixture in the rig, in strip-node order.
    class FixtureMap
    {
    public:
        void addFixture(const Fixture& fixture) { fixtures.push_back(fixture); }

        size_t size() const { return fixtures.size(); }
        const Fixture& operator[](size_t idx) const { return fixtures[idx]; }
        const std::vector<Fixture>& all() const { return fixtures; }

        /// Position of fixture `idx` along the rig, normalised 0..1. Uses the
        /// configured position when there is one, otherwise even spacing.
        float normalizedPosition(size_t idx) const;

        /// Writes `colors` (one per fixture, index-aligned) into `universe`,
        /// applying per-fixture trim, master brightness and gamma.
        /// Colours beyond the fixture count are ignored; missing ones go dark.
        void render(const std::vector<ecore::HSV>& colors,
                    float masterBrightness,
                    float gamma,
                    DmxUniverse& universe) const;

        /// Reports fixtures that overlap each other or run past channel 512.
        /// Non-fatal by design: a rig mid-repatch should still light up.
        std::vector<std::string> validate() const;

    private:
        std::vector<Fixture> fixtures;
    };
}
