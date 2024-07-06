// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jeffects.h"

#include "jmath.h"
#include <cmath>

using namespace j;

FireEmitter::FireEmitter(uint16_t inLength) : length(inLength)
{
    heatValues.resize(inLength);
}


void FireEmitter::tick(float deltaTime)
{
    // step 1: apply cooldown
    for(int i = 1; i < length; ++i)
    {
        heatValues[i] -= deltaTime * cooldown;
        heatValues[i] = std::clamp(heatValues[i], 0.0f, 1.0f);
    }

    // step 2: flame source (only on idx 0)
    flameSourceSaw.tick(deltaTime);

    float currentFlameSource = lerp_keyframes(flameSourceSaw.evaluate(0.0f), flameSourceAnim);
    heatValues[0] = currentFlameSource * flameSourcePower;

    // step 3: spread flame
    for(int i = 1; i < length; ++i)
    {
        heatValues[i] = heatValues[i-1] * spreadRate * deltaTime;
    }
}

float FireEmitter::evaluate(float val) const
{
    /*
    if(bReverse)
    {
        value = size - value;
    }

    value = std::clamp(value, 0.0f, (float)size);

    float alpha = std::fmod(value, 1.0f);
    int idx = value;
    if(idx > size - 2 || (idx > size - 1 && ess_equal(alpha, 0.0f)))
    {
        return 0.0f;
    }

    float evaluation = (heatValues[idx] * (1.0f - alpha)) + (heatValues[idx + 1] * alpha);
    evaluation = std::clamp(evaluation, 0.0f, 1.0f);

    return evaluation;
    */

   //TODO LOGIC
   //return std::clamp(heatValues[idx], 0.0f, 1.0f);
   return 0.0f;
}
