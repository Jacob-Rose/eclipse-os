// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_spellcaster.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/spellbook.h"

State_Spellcaster::State_Spellcaster(const char* InStateName) : State(InStateName)
{

}

void State_Spellcaster::onStateBegin()
{
    State::onStateBegin();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)spellbook, sizeof(spellbook));
}

void State_Spellcaster::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    float deltaTime = lastFrameDT.count();

    lfo1.tick(deltaTime);
    lfo2.tick(deltaTime);

    for(int i = 0; i < GM.OutfitLEDs->getLength(); ++i)
    {
        float combinedValues = lfo1.evaluate(i) + lfo2.evaluate(i);
        float val = std::clamp(combinedValues, 0.0f, 1.0f);
        j::HSV color = palette.getColor(val);

        GM.OutfitLEDs->setHSV(i, color);
    }
    
    for(int i = 0; i < GM.RingLEDs->getLength(); ++i)
    {
        float combinedValues = lfo1.evaluate(i) + lfo2.evaluate(i);
        float val = std::clamp(combinedValues, 0.0f, 1.0f);
        j::HSV color = palette.getColor(val);

        GM.RingLEDs->setHSV(i, color);
    }
}