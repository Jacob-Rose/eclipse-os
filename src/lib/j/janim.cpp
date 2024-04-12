// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "janim.h"

using namespace j;

Spring::Spring()
{

}

LFO::LFO(float inSpeed, float inWidth, float inInitialOffset) : speed(inSpeed), width(inWidth), initialOffset(inInitialOffset)
{

}

void LFO::tick(float deltaTime)
{
    currentOffset += deltaTime * speed;
}

float LFO::evaluate(float inVal)
{
    float evaluatedOffset = (inVal / width * PI * 2.f); // 2. makes it so that the width goes a full cycle instead of half
    float sinVal = std::sin(evaluatedOffset + currentOffset + initialOffset);
    return (sinVal / 2) + 0.5f;
}