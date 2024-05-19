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
    template<typename T>
    class Generator
    {
        virtual T evaluate(float val) const = 0;
    };

    class FloatAttribute
    {
        virtual float getCurrentPosition() const = 0;
    };

    // similar to ADS in a FPS, we will blend consistantly to our target value
    // contains momentum system
    class MomentumFloat : public FloatAttribute
    {
    public:
        MomentumFloat(float inAcceleration, float inMaxSpeed);

        void tick(float deltaTime);
        virtual float getCurrentPosition() const override { return currentPosition; }

        float targetPosition;
        
        float acceleration;
        float maxSpeed;
    private:
        float currentSpeed = 0.0f;
        float currentPosition = 0.0f;
    };

    class Spring : public FloatAttribute
    {
    public:
        Spring(float inStrengthForce, float inDampening = 0.8f);
        
        void tick(float deltaTime);

        virtual float getCurrentPosition() const override { return currentPosition; }

        float targetPosition = 0.0f;

        float strengthForce;
        float dampening;

    private:
        float currentAcceleration = 0;
        float currentPosition = 0;
    };

    // Normalized 0-1 sin wave calculator with support for dynamic offsets
    // also normalizes width so the width is the lenth of the full sine wave
    class LFO : public Generator<float>
    {
    public:
        LFO();
        LFO(float inSpeed, float inWidth);

        void tick(float deltaTime);

        virtual float evaluate(float val) const override;

        float getCurrentOffset() const { return currentOffset; }

        float speed = 1.0f;
        float width = 1.0f;
        float initialOffset = 0.0f;
    private:
        float currentOffset = 0.0f;
    };


    class Saw : public Generator<float>
    {
    public:
        Saw();
        Saw(float inSpeed);

        void tick(float deltaTime);

        // val is added to the currentOffset in this case
        virtual float evaluate(float val) const override;

        float getCurrentOffset() const { return currentOffset; }

        float speed = 1.0f;
        float width = 1.0f;
        float initialOffset = 0.0f;
        float gapWidth = 0.0f;
    private:
        float currentOffset = 0.0f;
    };



}