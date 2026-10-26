// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include "ecore/fp.h"
#include "ecore/hsv.h"

using namespace efp;
using namespace ecore;

// The constexpr contract is pinned by the static_asserts in fp.h. These cover
// what those cannot: float round trips, and the colour type built on top.

TEST(fp_unit_round_trips_within_resolution)
{
    for (int i = 0; i <= 100; ++i)
    {
        const float f = i / 100.0f;
        CHECK_NEAR(toFloat(fromUnit(f)), f, 1.0f / ONE);
    }
}

TEST(fp_turns_wrap_rather_than_clamp)
{
    CHECK_EQ(fromTurns(2.0f), 0);
    CHECK_EQ(fromTurns(-1.0f), 0);
    CHECK_NEAR(toFloat(fromTurns(3.75f)), 0.75f, 1.0f / ONE);
    CHECK_NEAR(toFloat(fromTurns(-0.1f)), 0.9f, 1.0f / ONE);
}

TEST(fp_lerp_is_monotonic)
{
    fpInt last = 0;
    for (fpInt t = 0; t < ONE; t += ONE / 64)
    {
        const fpInt now = lerp(0, ONE, t);
        CHECK(now >= last);
        last = now;
    }
    CHECK_EQ(lerp(0, ONE, ONE), ONE);
}

TEST(hsv_degrees_round_trip)
{
    const HSV red(0.0f, 1.0f, 1.0f);
    const HSV cyan(180.0f, 1.0f, 1.0f);
    const HSV almost(359.0f, 0.5f, 0.25f);

    CHECK_NEAR(red.getHueFloat(), 0.0f, 0.05f);
    CHECK_NEAR(cyan.getHueFloat(), 180.0f, 0.05f);
    CHECK_NEAR(almost.getHueFloat(), 359.0f, 0.05f);
    CHECK_NEAR(almost.getSatFloat(), 0.5f, 0.001f);
    CHECK_NEAR(almost.getValFloat(), 0.25f, 0.001f);

    // 360 is 0, and negative degrees come round the other way
    CHECK_NEAR(HSV(360.0f, 1.0f, 1.0f).getHueFloat(), 0.0f, 0.05f);
    CHECK_NEAR(HSV(-90.0f, 1.0f, 1.0f).getHueFloat(), 270.0f, 0.05f);
}

TEST(hsv_hardware_conversions_hit_the_endpoints)
{
    const HSV full(0.0f, 1.0f, 1.0f);
    const HSV off(0.0f, 0.0f, 0.0f);
    const HSV half(180.0f, 0.5f, 0.5f);

    CHECK_EQ(full.getSatAs8(), 255);
    CHECK_EQ(full.getValAs8(), 255);
    CHECK_EQ(off.getSatAs8(), 0);
    CHECK_EQ(off.getValAs8(), 0);
    CHECK_EQ(half.getSatAs8(), 128);
    CHECK_EQ(half.getHueAs16(), 32768);
    CHECK_EQ(off.getHueAs16(), 0);
}

TEST(hsv_blend_takes_the_short_way_round)
{
    const HSV a(350.0f, 1.0f, 1.0f);
    const HSV b(10.0f, 1.0f, 1.0f);

    CHECK_NEAR(HSV::blend(a, b, 0.5f).getHueFloat(), 0.0f, 0.1f);
    CHECK_NEAR(HSV::blend(b, a, 0.5f).getHueFloat(), 0.0f, 0.1f);

    // a quarter of the way from 350 toward 10 is 355, not somewhere near 265
    CHECK_NEAR(HSV::blend(a, b, 0.25f).getHueFloat(), 355.0f, 0.1f);

    // stops more than 180 degrees apart go the other way round: 0 -> 270
    // through magenta at 315, not through green at 135
    const HSV red(0.0f, 1.0f, 1.0f);
    const HSV violet(270.0f, 1.0f, 1.0f);
    CHECK_NEAR(HSV::blend(red, violet, 0.5f).getHueFloat(), 315.0f, 0.1f);
    CHECK_NEAR(HSV::blend(violet, red, 0.5f).getHueFloat(), 315.0f, 0.1f);

    // exactly opposite hues: either way is as short, and it must still
    // land on the circle rather than somewhere off it
    const HSV cyan(180.0f, 1.0f, 1.0f);
    const float opposite = HSV::blend(red, cyan, 0.5f).getHueFloat();
    CHECK(std::fabs(opposite - 90.0f) < 0.1f || std::fabs(opposite - 270.0f) < 0.1f);
}

TEST(hsv_blend_endpoints_and_channels)
{
    const HSV a(30.0f, 0.2f, 0.8f);
    const HSV b(90.0f, 0.6f, 0.4f);

    const HSV at0 = HSV::blend(a, b, 0.0f);
    const HSV at1 = HSV::blend(a, b, 1.0f);
    const HSV mid = HSV::blend(a, b, 0.5f);

    CHECK_EQ(at0.h, a.h);
    CHECK_EQ(at0.s, a.s);
    CHECK_EQ(at0.v, a.v);
    CHECK_EQ(at1.h, b.h);
    CHECK_EQ(at1.s, b.s);
    CHECK_EQ(at1.v, b.v);
    CHECK_NEAR(mid.getHueFloat(), 60.0f, 0.1f);
    CHECK_NEAR(mid.getSatFloat(), 0.4f, 0.001f);
    CHECK_NEAR(mid.getValFloat(), 0.6f, 0.001f);

    // out-of-range alpha clamps instead of extrapolating
    CHECK_EQ(HSV::blend(a, b, -2.0f).v, a.v);
    CHECK_EQ(HSV::blend(a, b, 5.0f).v, b.v);
}

TEST(hsv_add_clamps_and_averages_hue)
{
    const HSV a(340.0f, 0.7f, 0.9f);
    const HSV b(20.0f, 0.7f, 0.9f);
    const HSV sum = HSV::add(a, b);

    CHECK_NEAR(sum.getHueFloat(), 0.0f, 0.1f);
    CHECK_EQ(sum.s, ONE);
    CHECK_EQ(sum.v, ONE);

    const HSV dim = HSV::add(HSV(0.0f, 0.25f, 0.25f), HSV(0.0f, 0.25f, 0.5f));
    CHECK_NEAR(dim.getSatFloat(), 0.5f, 0.001f);
    CHECK_NEAR(dim.getValFloat(), 0.75f, 0.001f);
}

TEST(hsv_palette_interpolates_between_stops)
{
    const HSVPalette rainbow({HSV(0.0f, 1.0f, 1.0f), HSV(120.0f, 1.0f, 1.0f), HSV(240.0f, 1.0f, 1.0f)});

    CHECK_NEAR(rainbow.getColor(0.0f).getHueFloat(), 0.0f, 0.1f);
    CHECK_NEAR(rainbow.getColor(0.25f).getHueFloat(), 60.0f, 0.1f);
    CHECK_NEAR(rainbow.getColor(0.5f).getHueFloat(), 120.0f, 0.1f);
    CHECK_NEAR(rainbow.getColor(1.0f).getHueFloat(), 240.0f, 0.1f);
}
