// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_bluemagic.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/mage-spell.h"

State_BlueMagic::State_BlueMagic(const char* InStateName) : State(InStateName)
{

}

void State_BlueMagic::onStateBegin()
{
    State::onStateBegin();
    
    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)mage_spell, sizeof(mage_spell));
}

void State_BlueMagic::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();
    float deltaTime = lastFrameDT.count();

    fireOffset.tick(deltaTime);
    lfoInchwormSpeed.tick(deltaTime);
    lfoNecklaceOuter.speed = remap(0.0f, 1.0f, -inchwormSpeed, inchwormSpeed, lfoInchwormSpeed.evaluate(1.0f));
    lfoNecklaceOuter.tick(deltaTime);

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        GM.RingLEDs->setHSV(idx, j::HSV(0,0,0));
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        float lfo = lfoNecklaceOuter.evaluate(idx);
        j::HSV color = firePalette.getColor(lfo);
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, color);
    }

    for(int i = 0; i < GM.OutfitLEDs->getLength(); ++i)
    {
        j::HSV color = firePalette.getColor(fireOffset.evaluate(i));
        GM.OutfitLEDs->setHSV(i, color);
    }
}