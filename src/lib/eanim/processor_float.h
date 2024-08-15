// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <memory>

#include "attribute_float.h"
#include "../ecore/tickable.h"

using namespace ecore;

namespace eanim
{
    /* @brief FloatProcessor is the base interface for any float attribute that will do some processing of the input over time
    * 
    * @see Sweep
    * @see Spring
    */
    class FloatProcessor : public FloatAttribute
    {
    public:
        virtual void setTargetValue(float inTargetValue) = 0;
    };

    
    /* @brief most basic usage of a float attribute, just provides a static value
    *
    * no technical limitation that this remains static, but thats its intent
    */
    class LiteralFloat final : public FloatProcessor
    {
    public:
        virtual void setTargetValue(float inTargetValue) override { value = inTargetValue; }
        virtual float getValue() const override { return value; }

    private:
        float value;
    };


    /* @brief Values will aim to move to the target value using acceleration.
    * TODO: additional features to support overshoot slightly and dampen quickly
    *
    * contains rudimentry momentum system
    * TODO: make this not borked
    */ 
    class Momentum : public FloatProcessor, public Tickable
    {
    public:
        Momentum();
        Momentum(float inAcceleration, float inMaxSpeed);

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
    * TODO: make this not borked
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

    /* @brief Wrapper for Float Attribute so that we can make these work similar to instanced uobjects in unreal
    */
    class AttributeBuilder
    {
        static std::shared_ptr<FloatAttribute> makeLiteral(float value);
    };
}