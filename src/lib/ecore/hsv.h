// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <utility>
#include <vector>
#include <string>


namespace ecore
{
    // originally taken from Adafruit NeoPixel
    static const uint8_t PROGMEM GammaBrightnessCorrection[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   2,   3,
    3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   5,   6,
    6,   6,   6,   7,   7,   7,   8,   8,   8,   9,   9,   9,   10,  10,  10,
    11,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,
    17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  24,  24,  25,
    25,  26,  27,  27,  28,  29,  29,  30,  31,  31,  32,  33,  34,  34,  35,
    36,  37,  38,  38,  39,  40,  41,  42,  42,  43,  44,  45,  46,  47,  48,
    49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
    64,  65,  66,  68,  69,  70,  71,  72,  73,  75,  76,  77,  78,  80,  81,
    82,  84,  85,  86,  88,  89,  90,  92,  93,  94,  96,  97,  99,  100, 102,
    103, 105, 106, 108, 109, 111, 112, 114, 115, 117, 119, 120, 122, 124, 125,
    127, 129, 130, 132, 134, 136, 137, 139, 141, 143, 145, 146, 148, 150, 152,
    154, 156, 158, 160, 162, 164, 166, 168, 170, 172, 174, 176, 178, 180, 182,
    184, 186, 188, 191, 193, 195, 197, 199, 202, 204, 206, 209, 211, 213, 215,
    218, 220, 223, 225, 227, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252,
    255};

    struct HSV
    {
        HSV();
        HSV(float inH, float inS, float inV);

        HSV blendWith(HSV otherColor, float alpha);

        // set between 0.f - 360.f
        void setHueDegree(float val);
        void setSaturationAlpha(float val);
        void setBrightnessAlpha(float val);

        uint16_t getHueAs16() const;
        uint8_t getSatAs8() const;
        uint8_t getValAs8() const;

        std::string to_string() const;
        
        float h; // 0.f - 360.0f in degrees, clamps itself and wraps around in setHueDegree()
        float s; // 0.f - 1.f
        float v; // 0.f - 1.f
    };

    //TODO support hsv wrapping around 0 for hue
    struct HSVPalette
    {
        HSVPalette();
        HSVPalette(const std::vector<HSV>& inColors);
        HSVPalette(const std::initializer_list<HSV>& inColors);

        // scale is from 0-1.f
        // supports looping back to the original color
        HSV getColor(float val) const;

        std::vector<HSV> colors;
    };
}