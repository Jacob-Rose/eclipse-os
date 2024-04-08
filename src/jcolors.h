// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <utility>
#include <vector>

namespace j
{
    struct HSV
    {
        HSV();
        HSV(uint16_t inH, uint8_t inS, uint8_t inV);
        HSV(float inH, float inS, float inV);
        
        uint16_t h; 
        uint8_t s; 
        uint8_t v;
    };


    struct HSVPalette
    {
        HSVPalette();
        HSVPalette(const std::vector<HSV>& inColors);

        // scale is from 0-1.f
        // supports looping back to the original color
        HSV getColor(float val) const;

        std::vector<HSV> colors;
    };
}