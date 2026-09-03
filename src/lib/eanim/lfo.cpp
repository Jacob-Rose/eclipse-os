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