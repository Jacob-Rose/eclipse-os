// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>
#include <ecore/tickable.h>

#include "attribute_float.h"

using namespace ecore;

namespace eanim
{
    /* @brief A float that chases a target over time.
    *
    * Set a target, tick it every frame, read the value. Each processor gets
    * there in its own way: Lerper on a schedule, Momentum like a motor with a
    * speed limit, Spring with a bounce.
    *
    * @see Lerper
    * @see Momentum
    * @see Spring
    */
    class FloatProcessor : public FloatAttribute
    {
    public:
        virtual void setTargetValue(float inTargetValue) = 0;
    };


    /* @brief A constant. The target is the value, immediately. */
    class LiteralFloat final : public FloatProcessor
    {
    public:
        LiteralFloat() = default;
        explicit LiteralFloat(float inValue) : value(inValue) {}

        void setTargetValue(float inTargetValue) override { value = inTargetValue; }
        float getValue() const override { return value; }

    private:
        float value{0.0f};
    };


    /* @brief Moves to the target in a fixed time, in a straight line.
    *
    * A new target starts from wherever the value is now, so retargeting
    * mid-lerp never jumps. The duration given to startLerp sticks for later
    * setTargetValue calls.
    *
    *   fade.reset(0.0f);
    *   fade.startLerp(1.0f, 0.5f);   // half a second to full
    *   fade.tick(deltaTime);
    *   brightness *= fade.getValue();
    */
    class Lerper : public FloatProcessor, public Tickable
    {
    public:
        Lerper() = default;
        explicit Lerper(float inValue) { reset(inValue); }

        /// Jumps straight to a value, no lerp.
        void reset(float inValue);

        /// Heads for a target over the given seconds; zero or less snaps.
        void startLerp(float inTargetValue, float inDuration);
        /// Heads for a target over the last duration used (1s before any).
        void setTargetValue(float inTargetValue) override { startLerp(inTargetValue, duration); }

        void tick(float deltaTime) override;
        float getValue() const override;

        /// 0 at the start of a lerp, 1 once it has arrived.
        float getProgress() const;
        bool isLerping() const { return elapsed < duration; }
        float getTargetValue() const { return targetValue; }

    private:
        float startValue{0.0f};
        float targetValue{0.0f};
        float duration{1.0f};
        float elapsed{1.0f};
    };


    /* @brief Moves to the target like a motor: speeds up, cruises at the speed
    * limit, brakes, and stops exactly on it without overshooting.
    *
    * Both rates are per second: acceleration in units/s^2, maxSpeed in units/s.
    * A big acceleration with a low maxSpeed is a constant-speed slide; a low
    * acceleration is a slow lean into the move.
    *
    *   head.reset(0.0f);
    *   head.setTargetValue(strip.length());
    *   head.tick(deltaTime);
    */
    class Momentum : public FloatProcessor, public Tickable
    {
    public:
        Momentum() = default;
        Momentum(float inAcceleration, float inMaxSpeed)
            : acceleration(inAcceleration), maxSpeed(inMaxSpeed) {}

        float acceleration{10.0f};
        float maxSpeed{5.0f};

        /// Jumps straight to a value and stops.
        void reset(float inValue);

        void setTargetValue(float inTargetValue) override { targetValue = inTargetValue; }
        void tick(float deltaTime) override;
        float getValue() const override { return currentValue; }

        float getTargetValue() const { return targetValue; }
        float getSpeed() const { return currentSpeed; }
        bool isSettled() const { return currentValue == targetValue && currentSpeed == 0.0f; }

    private:
        float targetValue{0.0f};
        float currentValue{0.0f};
        float currentSpeed{0.0f};
    };


    /* @brief Moves to the target on a damped spring: the bounce UX designers love.
    *
    * stiffness is how hard it pulls (higher is faster; the natural frequency is
    * sqrt(stiffness) rad/s, so 100 rings about every 0.6s). damping is a ratio:
    * 1 is critically damped and settles with no overshoot, below 1 bounces,
    * above 1 is sluggish.
    *
    *   bob.reset(0.0f);
    *   bob.stiffness = 200.0f;
    *   bob.damping = 0.5f;          // a couple of visible bounces
    *   bob.setTargetValue(1.0f);
    *   bob.tick(deltaTime);
    */
    class Spring : public FloatProcessor, public Tickable
    {
    public:
        Spring() = default;
        Spring(float inStiffness, float inDamping = 0.8f)
            : stiffness(inStiffness), damping(inDamping) {}

        float stiffness{10.0f};
        float damping{0.8f};

        /// Jumps straight to a value and comes to rest.
        void reset(float inValue);

        void setTargetValue(float inTargetValue) override { targetValue = inTargetValue; }
        void tick(float deltaTime) override;
        float getValue() const override { return currentValue; }

        float getTargetValue() const { return targetValue; }
        float getVelocity() const { return velocity; }
        /// Within tolerance of the target and nearly still.
        bool isSettled(float tolerance = 0.001f) const;

    private:
        float targetValue{0.0f};
        float currentValue{0.0f};
        float velocity{0.0f};
    };
}
