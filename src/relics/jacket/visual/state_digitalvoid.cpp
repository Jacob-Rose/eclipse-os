// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_digitalvoid.h"

void Pattern_DigitalVoid::init()
{
    coreNoise.imageScaleX = 100.0f;
    coreNoise.imageScaleY = 100.0f;
    coreNoise.timeScale = 25.0f;

    hueShiftNoise.imageScaleX = 25.0f;
    hueShiftNoise.imageScaleY = 25.0f;
}

void Pattern_DigitalVoid::tick(float deltaTime)
{
    coreNoise.tick(deltaTime);
    hueShiftNoise.tick(deltaTime);
}

void State_DigitalVoid::init()
{
}
