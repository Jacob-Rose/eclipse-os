// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "processor_float.h"

#include <algorithm>
#include <cmath>

using namespace eanim;
using namespace ecore;

// ---- Lerper ---------------------------------------------------------------------

void Lerper::reset(float inValue)
{
    startValue = inValue;
    targetValue = inValue;
    elapsed = duration;
}

void Lerper::startLerp(float inTargetValue, float inDuration)
{
    startValue = getValue();
    targetValue = inTargetValue;
    duration = std::max(inDuration, 0.0f);
    elapsed = 0.0f;
}

void Lerper::tick(float deltaTime)
{
    if (elapsed < duration)
    {
        elapsed = std::min(elapsed + deltaTime, duration);
    }
}

float Lerper::getProgress() const
{
    if (duration <= 0.0f) return 1.0f;
    return std::clamp(elapsed / duration, 0.0f, 1.0f);
}

float Lerper::getValue() const
{
    return std::lerp(startValue, targetValue, getProgress());
}

// ---- Momentum -------------------------------------------------------------------

void Momentum::reset(float inValue)
{
    currentValue = inValue;
    targetValue = inValue;
    currentSpeed = 0.0f;
}

void Momentum::tick(float deltaTime)
{
    if (deltaTime <= 0.0f || isSettled()) return;

    const float remaining = targetValue - currentValue;
    if (acceleration <= 0.0f || maxSpeed <= 0.0f)
    {
        currentValue = targetValue;   // no way to move gradually: arrive
        currentSpeed = 0.0f;
        return;
    }

    // brake once the stopping distance at the current speed reaches the target,
    // otherwise push toward it; either way the speed limit holds
    const float direction = remaining < 0.0f ? -1.0f : 1.0f;
    const float stoppingDistance = (currentSpeed * currentSpeed) / (2.0f * acceleration);
    const bool movingToward = currentSpeed * direction > 0.0f;
    const bool shouldBrake = movingToward && stoppingDistance >= std::fabs(remaining);
    currentSpeed += (shouldBrake ? -direction : direction) * acceleration * deltaTime;
    currentSpeed = std::clamp(currentSpeed, -maxSpeed, maxSpeed);

    // land exactly rather than stepping over the target
    const float step = currentSpeed * deltaTime;
    if (currentSpeed * direction > 0.0f && std::fabs(step) >= std::fabs(remaining))
    {
        currentValue = targetValue;
        currentSpeed = 0.0f;
        return;
    }
    currentValue += step;
}

// ---- Spring ---------------------------------------------------------------------

void Spring::reset(float inValue)
{
    currentValue = inValue;
    targetValue = inValue;
    velocity = 0.0f;
}

bool Spring::isSettled(float tolerance) const
{
    return std::fabs(targetValue - currentValue) <= tolerance && std::fabs(velocity) <= tolerance;
}

void Spring::tick(float deltaTime)
{
    if (deltaTime <= 0.0f) return;
    if (stiffness <= 0.0f)
    {
        reset(targetValue);
        return;
    }

    // semi-implicit Euler, substepped so a stiff spring stays stable on a slow frame
    const float omega = std::sqrt(stiffness);
    const float dampingCoefficient = 2.0f * std::max(damping, 0.0f) * omega;
    const int steps = std::clamp(static_cast<int>(std::ceil(omega * deltaTime / 0.25f)), 1, 64);
    const float dt = deltaTime / steps;
    for (int i = 0; i < steps; ++i)
    {
        const float accelerationNow = stiffness * (targetValue - currentValue) - dampingCoefficient * velocity;
        velocity += accelerationNow * dt;
        currentValue += velocity * dt;
    }
}
