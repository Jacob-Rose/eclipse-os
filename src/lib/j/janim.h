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
    // similar to ADS in a FPS, we will blend consistantly to our target value
    // contains momentum system
    class MomentumFloat
    {
    public:
        MomentumFloat(float inAcceleration, float inMaxSpeed);

        void tick(float deltaTime);
        float getCurrentPosition() const { return currentPosition; }

        float targetPosition;
        
        float acceleration;
        float maxSpeed;
    private:
        float currentSpeed = 0.0f;
        float currentPosition = 0.0f;
    };

    class Spring
    {
    public:
        Spring(float inStrengthForce, float inDampening = 0.8f);
        
        void tick(float deltaTime);

        float getCurrentPosition() const { return currentPosition; }

        float targetPosition = 0.0f;

        float strengthForce;
        float dampening;

    private:
        float currentAcceleration = 0;
        float currentPosition = 0;
    };

    // Normalized 0-1 sin wave calculator with support for dynamic offsets
    // also normalizes width so the width is the lenth of the full sine wave
    class LFO
    {
    public:
        LFO();
        LFO(float inSpeed, float inWidth);

        void tick(float deltaTime);

        float evaluate(float val) const;

        float getCurrentOffset() const { return currentOffset; }

        float speed = 0.0f;
        float width = 2.0f;
        float initialOffset = 0.0f;
    private:
        float currentOffset = 0.0f;
    };


    class Saw
    {
    public:
        Saw();
        Saw(float inSpeed);

        void tick(float deltaTime);

        // val is added to the currentOffset
        float evaluate(float val) const;

        float getCurrentOffset() const { return currentOffset; }

        float speed = 0.0f;
        float width = 1.0f;
        float initialOffset = 0.0f;
        float gapWidth = 0.0f;
    private:
        float currentOffset = 0.0f;
    };
}