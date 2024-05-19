// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include "jcolors.h"

// palettes acquired through https://colorhunt.co/palettes/popular 

namespace j
{
    inline HSVPalette p_retrosunset {
        HSV(55.0f, 0.578f, 0.976f),
        HSV(18.0f, 0.612f, 0.941f),
        HSV(343.0f, 0.679f, 0.722f),
        HSV(295.0f, 0.607f, 0.439f),
    };

    inline HSVPalette p_bluemagic {
        HSV(219.0f, 0.899f, 0.624f),
        HSV(268.0f, 0.627f, 0.8f),
        HSV(295.0f, 0.56f, 0.812f),
        HSV(327.0f, 0.492f, 0.949f),
    };

    inline HSVPalette p_disney100 {
        HSV(249.0f, 0.495f, 0.753f),
        HSV(257.0f, 0.431f, 0.91f),
        HSV(178.0f, 0.401f, 0.91f),
        HSV(141.0f, 0.165f, 1.f),
    };

    
    inline HSVPalette p_purplesky {
        HSV(280.0f, 0.86f, 0.75f),
        HSV(265.0f, 0.75f, 0.2f),
    };

    inline HSVPalette p_iceCream {
        HSV(321.0f, 0.537f, 1.f),
        HSV(62.0f, 0.239f, 1.f),
        HSV(158.0f, 0.42f, 1.f),
        HSV(205.0f, 0.361f, 1.f),
    };

    inline HSVPalette p_ritual {
        HSV(337.0f, 0.917f, 0.565f),
        HSV(343.0f, 1.f, 0.78f),
        HSV(15.0f, 0.936f, 0.976f),
        HSV(53.0f, 0.863f, 0.973f),
    };

    inline HSVPalette p_naturenight {
        HSV(224.0f, 0.667f, 0.388f),
        HSV(166.0f, 0.742f, 0.349f),
        HSV(80.0f, 0.535f, 0.675f),
        HSV(59.0f, 0.626f, 0.827f),
    };

    inline HSVPalette p_bootgradient {
        HSV(0.0f, 0.8f, 1.0f),
        HSV(133.0f, 0.8f, 1.0f)
    };
};