// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include "edmx/fixture.h"
#include "edmx/output.h"
#include "edmx/universe.h"

using namespace ecore;
using namespace edmx;

TEST(edmx_channel_buffer_is_one_based_and_bounds_checked)
{
    ChannelBuffer buffer(8);
    CHECK_EQ(buffer.size(), 8u);

    buffer.setChannel(1, 10);
    buffer.setChannel(8, 80);
    buffer.setChannel(0, 99);   // ignored
    buffer.setChannel(9, 99);   // ignored
    CHECK_EQ(buffer.getChannel(1), 10);
    CHECK_EQ(buffer.getChannel(8), 80);
    CHECK_EQ(buffer.getChannel(0), 0);
    CHECK_EQ(buffer.getChannel(9), 0);
    CHECK_EQ(buffer.data()[0], 10);

    buffer.clear();
    CHECK_EQ(buffer.getChannel(1), 0);

    buffer.resize(0);           // never empty
    CHECK_EQ(buffer.size(), 1u);

    DmxUniverse universe;
    CHECK_EQ(universe.size(), static_cast<size_t>(DMX_CHANNEL_COUNT));
}

TEST(edmx_hsv_to_rgb8_hits_the_primaries)
{
    const Rgb8 red = hsvToRgb8(HSV(0.0f, 1.0f, 1.0f));
    CHECK_EQ(red.r, 255); CHECK_EQ(red.g, 0); CHECK_EQ(red.b, 0);

    const Rgb8 green = hsvToRgb8(HSV(120.0f, 1.0f, 1.0f));
    CHECK_EQ(green.r, 0); CHECK_EQ(green.g, 255); CHECK_EQ(green.b, 0);

    const Rgb8 blue = hsvToRgb8(HSV(240.0f, 1.0f, 1.0f));
    CHECK_EQ(blue.r, 0); CHECK_EQ(blue.g, 0); CHECK_EQ(blue.b, 255);

    const Rgb8 grey = hsvToRgb8(HSV(0.0f, 0.0f, 0.5f));
    CHECK_EQ(grey.r, grey.g); CHECK_EQ(grey.g, grey.b);
    CHECK(grey.r >= 127 && grey.r <= 128);

    // gamma: 1 and <= 0 are identity, 2.2 darkens the middle
    CHECK_EQ(applyGamma(128, 1.0f), 128);
    CHECK_EQ(applyGamma(128, 0.0f), 128);
    CHECK(applyGamma(128, 2.2f) < 64);
    CHECK_EQ(applyGamma(255, 2.2f), 255);
}

TEST(edmx_channel_order_resolves_or_refuses)
{
    Fixture fixture;
    std::string error;

    fixture.channelOrder = "GRB";
    CHECK(resolveChannelOrder(fixture, error));
    CHECK_EQ(fixture.offsetR, 1); CHECK_EQ(fixture.offsetG, 0); CHECK_EQ(fixture.offsetB, 2);
    CHECK_EQ(fixture.channelOrder, std::string("grb"));

    fixture.channelOrder = "rgx";
    CHECK(!resolveChannelOrder(fixture, error));
    fixture.channelOrder = "rrb";
    CHECK(!resolveChannelOrder(fixture, error));
    fixture.channelOrder = "rg";
    CHECK(!resolveChannelOrder(fixture, error));
}

TEST(edmx_profiles_patch_at_an_address)
{
    FixtureProfile par;
    std::string error;
    CHECK(lookupBuiltinProfile("uking_par36", par));
    CHECK(validateProfile(par, error));
    CHECK(!lookupBuiltinProfile("not_a_par", par));

    const Fixture fixture = instantiateProfile(par, "par 1", 10);
    CHECK_EQ(fixture.startChannel, 10);
    CHECK_EQ(fixture.dimmerChannel, 10);         // offset 1
    CHECK_EQ(fixture.startChannel + fixture.offsetR, 11);
    CHECK_EQ(fixture.startChannel + fixture.offsetB, 13);
    CHECK_EQ(fixture.staticChannels.size(), 3u);  // 14, 15, 16 parked
    CHECK_EQ(fixtureHighestChannel(fixture), 16);

    // a profile that books one channel twice is refused
    FixtureProfile broken = par;
    broken.greenOffset = broken.redOffset;
    CHECK(!validateProfile(broken, error));
    CHECK(!error.empty());

    CHECK(!builtinProfileNames().empty());
    CHECK(describeProfile(par).find("dimmer") != std::string::npos);
}

TEST(edmx_fixture_map_renders_colours_dimmer_and_parks)
{
    FixtureProfile par;
    CHECK(lookupBuiltinProfile("uking_par36", par));

    FixtureMap map;
    map.addFixture(instantiateProfile(par, "a", 1));
    map.addFixture(instantiateProfile(par, "b", 8));
    CHECK_EQ(map.highestChannel(), 14);
    CHECK(map.validate().empty());

    ChannelBuffer buffer(DMX_CHANNEL_COUNT);
    std::vector<HSV> colors = {HSV(0.0f, 1.0f, 1.0f), HSV(120.0f, 1.0f, 0.5f)};
    map.render(colors, 1.0f, 0.0f, buffer);

    // fixture a at 1: dimmer 1, r 2, g 3, b 4, parked 5..7
    CHECK_EQ(buffer.getChannel(1), 255);
    CHECK_EQ(buffer.getChannel(2), 255);
    CHECK_EQ(buffer.getChannel(3), 0);
    CHECK_EQ(buffer.getChannel(5), 0);
    // fixture b at 8: green at half value
    CHECK_EQ(buffer.getChannel(9), 0);
    CHECK(buffer.getChannel(10) >= 127 && buffer.getChannel(10) <= 128);

    // master brightness scales, in HSV, before the bytes
    map.render(colors, 0.5f, 0.0f, buffer);
    CHECK(buffer.getChannel(2) >= 127 && buffer.getChannel(2) <= 128);

    // a missing colour renders dark but still parks
    map.render({}, 1.0f, 0.0f, buffer);
    CHECK_EQ(buffer.getChannel(2), 0);
    CHECK_EQ(buffer.getChannel(1), 255);

    // overlap is a warning, not a refusal
    map.addFixture(instantiateProfile(par, "c", 3));
    CHECK(!map.validate().empty());
    CHECK_NEAR(map.normalizedPosition(1), 0.5f, 1e-6f);
}

namespace
{
    // an output that just remembers the last frame, as a UART on a relic would
    struct MemoryOutput : public DmxOutput
    {
        std::vector<uint8_t> last;
        int length{DMX_CHANNEL_COUNT};
        bool opened{false};

        bool open(std::string&) override { opened = true; return true; }
        void close() override { opened = false; }
        bool isOpen() const override { return opened; }
        bool sendFrame(const ChannelBuffer& buffer, std::string&) override
        {
            const size_t n = std::min(buffer.size(), static_cast<size_t>(length));
            last.assign(buffer.data(), buffer.data() + n);
            return true;
        }
        void setUniverseLength(int channels) override { length = channels; }
        std::string describe() const override { return "memory"; }
    };
}

TEST(edmx_output_sends_the_universe_length_it_was_set)
{
    MemoryOutput output;
    std::string error;
    CHECK(output.open(error));

    ChannelBuffer buffer(DMX_CHANNEL_COUNT);
    buffer.setChannel(3, 42);
    output.setUniverseLength(24);
    CHECK(output.sendFrame(buffer, error));
    CHECK_EQ(output.last.size(), 24u);
    CHECK_EQ(output.last[2], 42);
    CHECK_NEAR(output.maxFrameRate(), 0.0f, 0.0f);
}
