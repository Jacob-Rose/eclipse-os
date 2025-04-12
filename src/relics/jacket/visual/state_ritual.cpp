// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_ritual.h"

#if 0

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/ritual-fast.h"

State_Ritual::State_Ritual(const char* InStateName) : State(InStateName)
{

}

void State_Ritual::onStateBegin()
{
    State::onStateBegin();
    
    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)ritual_fast, sizeof(ritual_fast));
}

void State_Ritual::tick()
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

#endif