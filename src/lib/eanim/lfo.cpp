// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "lfo.h"
#include <algorithm>
#include <cmath>

using namespace eanim;

LFO::LFO()
{

}

LFO::LFO(float inSpeed, float inWidth) : speed(inSpeed), width(inWidth)
{

}

void LFO::reflectState(ecore::PropertyBag& bag, const std::string& prefix)
{
    bag.addState((prefix + "offset").c_str(), currentOffset);
}

void LFO::tick(float deltaTime)
{
    currentOffset += deltaTime * speed;

    // Only ever goes into sin(), so it is a phase, and a phase wraps. Left to
    // grow it is a float losing its low bits: the whiteboard's rainbow, at 0.2
    // a second, reached 2^17 in a week - where a float's step is 0.016 and its
    // 0.006 tick rounds to nothing at all. It stopped there, exactly.
    constexpr float kTwoPi = 2.0f * 3.14159265f;
    currentOffset = std::fmod(currentOffset, kTwoPi);
}

float LFO::evaluate(float inVal) const
{
    float evaluatedOffset = (inVal / width * 3.14159265 * 2.f); // 2. makes it so that the width goes a full cycle instead of half
    float sinVal = std::sin(evaluatedOffset + currentOffset + xOffset);

    if(bUseEasingFunction)
    {
        sinVal = getEasingFunction(easingFunction)(sinVal);
    }

    if(bShouldReflect)
    {
        sinVal = std::abs(sinVal);
    }

    return (std::clamp((sinVal / 2) + 0.5f, 0.f, 1.0f) * amplitude) + yOffset;
}