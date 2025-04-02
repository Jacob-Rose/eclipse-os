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

void LFO::tick(float deltaTime) 
{
    currentOffset += deltaTime * speed;
}

float LFO::evaluate(float inVal) const
{
    float evaluatedOffset = (inVal / width * 3.14159265 * 2.f); // 2. makes it so that the width goes a full cycle instead of half
    float sinVal = std::sin(evaluatedOffset + currentOffset + valueOffset);
    if(bUseEasingFunction)
    {
        sinVal = getEasingFunction(easingFunction)(sinVal);
    }
    return (std::clamp((sinVal / 2) + 0.5f, 0.f, 1.0f) * amplitude) + heightOffset;
}