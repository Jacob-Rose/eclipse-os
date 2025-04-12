// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_digitalvoid.h"


#include "../../../imgs/enchanted-forest.h"

Pattern_DigitalVoid::Pattern_DigitalVoid()
{
}

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

void Pattern_DigitalVoid::render(HSVStripNode *inNode, HSV &inOutColor) const
{
    //todo
}

State_DigitalVoid::State_DigitalVoid(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_DigitalVoid::init()
{
    setStateStartGifData((uint8_t *)enchanted_forest, sizeof(enchanted_forest));

    std::shared_ptr<Pattern_DigitalVoid> pattern = std::make_shared<Pattern_DigitalVoid>();
    pattern->init();
    setGenerator(pattern);
}
