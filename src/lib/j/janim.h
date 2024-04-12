// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

namespace j
{

    //TODO
    class Spring
    {
        Spring();
    };

    // Normalized 0-1 sin wave calculator with support for dynamic offsets
    // also normalizes width so the width is the lenth of the full sine wave
    class LFO
    {
    public:
        LFO(float inSpeed, float inWidth, float inInitialOffset = 0.0f);

        void tick(float deltaTime);

        float evaluate(float val);

        float getCurrentOffset() const { return currentOffset; }

        float speed = 10.0f;
        float width = 1.0f;
        float initialOffset = 0.0f;
    private:
        float currentOffset = 0.0f;
    };
}