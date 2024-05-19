// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_enchantedforest.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/enchanted-forest.h"

State_EnchantedForest::State_EnchantedForest(const char* InStateName) : State(InStateName)
{

}

void State_EnchantedForest::onStateBegin()
{
    State::onStateBegin();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)enchanted_forest, sizeof(enchanted_forest));
}

void State_EnchantedForest::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    float deltaTime = lastFrameDT.count();

    lfo1.tick(deltaTime);
    lfo2.tick(deltaTime);

    for(int i = 0; i < GM.OutfitLEDs->getLength(); ++i)
    {
        float combinedValues = lfo1.evaluate(i) + lfo2.evaluate(i);
        combinedValues /= 2;
        j::HSV color = palette.getColor(combinedValues);

        GM.OutfitLEDs->setHSV(i, color);
    }
    
    for(int i = 0; i < GM.RingLEDs->getLength(); ++i)
    {
        float combinedValues = lfo1.evaluate(i) + lfo2.evaluate(i);
        combinedValues /= 2;
        j::HSV color = palette.getColor(combinedValues);

        GM.RingLEDs->setHSV(i, color);
    }
}