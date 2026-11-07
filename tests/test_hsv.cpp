// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <ecore/hsv.h>

using namespace ecore;

TEST(hsv_from_rgb_finds_the_primaries)
{
    CHECK_NEAR(HSV::fromRGB(255, 0, 0).getHueFloat(), 0.0f, 0.5f);
    CHECK_NEAR(HSV::fromRGB(0, 255, 0).getHueFloat(), 120.0f, 0.5f);
    CHECK_NEAR(HSV::fromRGB(0, 0, 255).getHueFloat(), 240.0f, 0.5f);
    CHECK_NEAR(HSV::fromRGB(255, 0, 255).getHueFloat(), 300.0f, 0.5f);
    CHECK_NEAR(HSV::fromRGB(255, 0, 0).getSatFloat(), 1.0f, 0.01f);
    CHECK_NEAR(HSV::fromRGB(255, 0, 0).getValFloat(), 1.0f, 0.01f);
}

TEST(hsv_from_rgb_handles_grey_and_black)
{
    CHECK_NEAR(HSV::fromRGB(128, 128, 128).getSatFloat(), 0.0f, 0.01f);
    CHECK_NEAR(HSV::fromRGB(128, 128, 128).getValFloat(), 128.0f / 255.0f, 0.01f);
    CHECK_NEAR(HSV::fromRGB(0, 0, 0).getValFloat(), 0.0f, 0.01f);
}

TEST(hsv_from_rgb_wraps_a_hue_below_red)
{
    CHECK_NEAR(HSV::fromRGB(255, 0, 128).getHueFloat(), 330.0f, 1.0f);
}
