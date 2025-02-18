// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "effects.h"

#include <cmath>

#include "../ecore/logging.h"
#include "../ecore/math.h"

using namespace ecore;
using namespace eanim;

FireGenerator::FireGenerator(uint16_t inLength)
{
    heatValues.resize(inLength);
}


void FireGenerator::tick(float deltaTime)
{
    /*
    // step 1: apply cooldown
    for(int i = 1; i < heatValues.size(); ++i)
    {
        heatValues[i] -= deltaTime * cooldown;
        heatValues[i] = std::clamp(heatValues[i], 0.0f, 1.0f);
    }

    // step 2: flame source (only on idx 0)
    flameSourceSaw.tick(deltaTime);

    float currentFlameSource = lerp_keyframes(flameSourceSaw.evaluate(0.0f), flameSourceAnim);
    heatValues[0] = flameSourcePower;

    // step 3: spread flame
    for(int i = 1; i < heatValues.size(); ++i)
    {
        heatValues[i] = heatValues[i-1] * spreadRate * deltaTime;
    }
    */

    heatValues[0] = 1.0f;
    heatValues[1] = 0.8f;
    heatValues[2] = 0.6f;
    heatValues[3] = 0.4f;
    heatValues[4] = 0.2f;
}

float FireGenerator::evaluate(float value) const
{
    int size = heatValues.size();

    if(bReverse)
    {
        value = size - value;
    }

    value = std::clamp(value, 0.0f, FLT_MAX);

    float alpha = std::fmod(value, 1.0f);
    int idx = value;

    if(ess_equal(alpha, 0.0f))
    {
        return heatValues[idx];
    }

    if(idx > size - 2)
    {
        return 0.0f;
    }

    float evaluation = (heatValues[idx] * (1.0f - alpha)) + (heatValues[idx + 1] * alpha);
    evaluation = std::clamp(evaluation, 0.0f, 1.0f);

    if(ess_equal(value, 0.0f))
    {
        // TODO use elog system
        //Serial.print(evaluation);
    }

    return evaluation;
}

/*


DropGenerator::DropGenerator(uint16_t inLength) : length(inLength)
{

}

void DropGenerator::tick(float deltaTime)
{
}

void DropGenerator::applyEffectLogic(std::vector<HSV> &InOutColors) const
{
}

*/

PerlinNoiseGenerator2D::PerlinNoiseGenerator2D()
{
    init();
}

void PerlinNoiseGenerator2D::init()
{
    currentTime = 0.0f;
    noise.SetSeed(rand());
}

void PerlinNoiseGenerator2D::tick(float deltaTime)
{
    currentTime += deltaTime;
}

float PerlinNoiseGenerator2D::evaluate(float x, float y) const
{
    float freqScalar = 1.0f / noise.GetFrequency();

    return noise.GetNoise<float>(x * imageScaleX, y * imageScaleY, currentTime * timeScale * freqScalar);
}
