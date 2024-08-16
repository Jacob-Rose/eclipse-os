// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "patterns.h"

#include "../lib/ecore/logging.h"

void Pattern_SpaceDust::init()
{
}

void Pattern_SpaceDust::tick(float deltaTime)
{
    
}

void Pattern_SpaceDust::applyEffectLogic(std::vector<HSV> &InOutColors) const
{
    for(int idx = 0; idx < InOutColors.size(); ++idx)
    {

    }
}


Pattern_RitualFire::Pattern_RitualFire() : fireGenerator(30)
{
    pulsePerlinNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    pulsePerlinNoise.noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Manhattan);
    pulsePerlinNoise.noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2);
    pulsePerlinNoise.noise.SetFrequency(0.05f);
    pulsePerlinNoise.noise.SetCellularJitter(1.0f);
    pulsePerlinNoise.timeScale = 0.33f;

    gasesPerlinNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    gasesPerlinNoise.noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Manhattan);
    gasesPerlinNoise.noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2Mul);
    gasesPerlinNoise.noise.SetFrequency(0.25f);
    gasesPerlinNoise.noise.SetCellularJitter(1.0f);
    //gasesPerlinNoise.timeScale = 5.0f;
}

void Pattern_RitualFire::init()
{
}

void Pattern_RitualFire::tick(float deltaTime)
{
    //fireGenerator.tick(deltaTime);
    pulsePerlinNoise.tick(deltaTime);
    //gasesPerlinNoise.tick(deltaTime);
}

void Pattern_RitualFire::applyEffectLogic(std::vector<HSV> &InOutColors) const
{
    for(int idx = 0; idx < InOutColors.size(); ++idx)
    {
        HSV outColor;

        //float fireValue = fireGenerator.evaluate(idx);
        //j::HSV fireColor = firePalette.getColor(fireValue);

        float pulseNoiseAlpha = pulsePerlinNoise.evaluate(idx);
        
        // layer 1: background cel pattern
        outColor = mainPalette.getColor(pulseNoiseAlpha);

        // layer 2: gas layer
        //float gasAlpha = gasesPerlinNoise.evaluate(idx);
        //outColor = outColor.blendWith(gasColor, gasAlpha);
        //outColor = outColor.blendWith(fireColor, fireValue);

        InOutColors[idx] = outColor;
    }
}