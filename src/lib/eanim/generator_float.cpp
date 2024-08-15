// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "generator_float.h"

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
