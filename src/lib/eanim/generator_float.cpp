// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "generator_float.h"
#include <algorithm>
#include <cmath>

using namespace eanim;

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

float Generator2D::evaluate(float x) const
{
    return evaluate(x, 0.0f);
}
