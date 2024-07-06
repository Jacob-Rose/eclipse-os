// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include "jtickable.h"

namespace j
{
    /* @brief FloatAttribute is the base for many float generators 
    * this gives us a uniform interface to build up a set of FloatAttributes and Generator1Ds 
    *
    * that can be fed into GeneratorHSVs
    */
    class FloatAttribute
    {
    public:
        virtual float getValue() const = 0;
    };

    /* @brief FloatProcessor is the base for any float attribute that will 
    * do some processing of the input over time or in general
    * 
    * @see Sweep
    * @see Spring
    */
    class FloatProcessor : public FloatAttribute
    {
    public:
        virtual void setTargetValue(float inTargetValue) = 0;
    };

    /* @brief Generator1D is meant to be treated like a process, made to take in a float
    * and generate a continous pattern from that float.
    *
    * For example, the provided float value could be the led index on the strip.
    * 
    * @see LFO
    * @see Saw
    */
    class Generator1D : public FloatAttribute
    {
    public:
        virtual float evaluate(float val) const = 0;

        // we can assume that these work as float attributes in most cases
        virtual float getValue() const override { return evaluate(0.0f); }
    };

    /* @brief similar to ADS in a FPS, we will blend consistantly to our target value
    *
    * contains rudimentry momentum system
    * TODO: make this not suck
    */ 
    class Sweep : public FloatProcessor, public Tickable
    {
    public:
        Sweep();
        Sweep(float inAcceleration, float inMaxSpeed);

        float acceleration;
        float maxSpeed;

    public:
        virtual void tick(float deltaTime) override;

        virtual float getValue() const override { return currentValue; }
        virtual void setTargetValue(float inTargetValue) override { targetValue = inTargetValue; }
    private:
        float targetValue = 0.0f;
        float currentSpeed = 0.0f;
        float currentValue = 0.0f;
    };

    /* @brief Spring will help round out values and give things that bounciness that UX designers love
    *
    */
    class Spring : public FloatProcessor, public Tickable
    {
    public:
        Spring();
        Spring(float inStrengthForce, float inDampening = 0.8f);

        float strengthForce = 10.0f;
        float dampening = 0.8f;
    public:
        virtual void tick(float deltaTime) override;

        virtual float getValue() const override { return currentValue; }
        virtual void setTargetValue(float inTargetValue) override { targetValue = inTargetValue; }

    private:
        float targetValue = 0.0f;
        float currentAcceleration = 0.0f;
        float currentValue = 0.0f;
    };

    /* @brief Spring will help round out values and give things that bounciness that UX designers love
    *
    */
    class LFO : public Generator1D, public Tickable
    {
    public:
        LFO();
        LFO(float inSpeed, float inWidth);

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // Generator1D interface
        virtual float evaluate(float val) const override;

        float getCurrentOffset() const { return currentOffset; }

        float speed = 1.0f;
        float width = 1.0f;
        float initialOffset = 0.0f;
    private:
        float currentOffset = 0.0f;
    };


    /* @brief Similar to an LFO, but linearly grows between 0-1, then clamps and resets back to zero 
    * the time it takes to reach 1 from 0 or vice versa = speed / fullWidth (fullWidth = width + gapWidth)
    * 
    * supports a gap between saw waves as well
    *
    */
    class Saw : public Generator1D, public Tickable
    {
    public:
        Saw();
        Saw(float inSpeed);

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // Generator1D interface
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