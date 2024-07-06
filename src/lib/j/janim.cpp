// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "janim.h"

#include "../../lib/j/jlogging.h"
#include "../../lib/j/jmath.h"

#include <algorithm>

using namespace j;

Sweep::Sweep()
{

}

Sweep::Sweep(float inAcceleration, float inMaxSpeed) 
    : acceleration(inAcceleration)
    , maxSpeed(inMaxSpeed)
{

}

void Sweep::tick(float deltaTime)
{
    float lastTickPosition = currentValue;

    float accelerationThisFrame = acceleration;
    if(targetValue < currentValue)
    {
        accelerationThisFrame *= -1.0f;
    }
    //currentSpeed += accelerationThisFrame * deltaTime; // this works for spring, will reuse
    currentSpeed = accelerationThisFrame;
    currentSpeed = std::clamp(currentSpeed, -maxSpeed, maxSpeed);
    currentValue += currentSpeed * deltaTime;
}

Spring::Spring(float inStrengthForce, float inDampening) : strengthForce(inStrengthForce), dampening(inDampening)
{

}

void Spring::tick(float deltaTime)
{
    float deltaPos = targetValue - currentValue;
    float force = deltaPos * strengthForce * dampening;
    currentAcceleration += force * deltaTime;
    currentValue += currentAcceleration * deltaTime;
}

LFO::LFO()
{

}

LFO::LFO(float inSpeed, float inWidth) : speed(inSpeed), width(inWidth)
{

}

void LFO::tick(float deltaTime) 
{
    currentOffset += deltaTime * speed;
}

float LFO::evaluate(float inVal) const
{
    float evaluatedOffset = (inVal / width * PI * 2.f); // 2. makes it so that the width goes a full cycle instead of half
    float sinVal = std::sin(evaluatedOffset + currentOffset + initialOffset);
    return (sinVal / 2) + 0.5f;
}

Saw::Saw()
{

}

Saw::Saw(float inSpeed) : speed(inSpeed)
{

}

void Saw::tick(float deltaTime)
{
    currentOffset += deltaTime * speed;
    float fullWidth = width + gapWidth;
    currentOffset = std::fmod(currentOffset, fullWidth);
}

float Saw::evaluate(float inVal) const
{
    float fullWidth = width + gapWidth;
    float newOffset = currentOffset + inVal;
    newOffset = std::fmod(newOffset, fullWidth);
    if(newOffset >= width || newOffset <= 0.0f)
    {
        return 0.0f;
    }
    else
    {
        float sawAlpha = newOffset / width;
        //jlog::print(std::to_string(sawAlpha));
        return 1.0f - sawAlpha;
    }
}

