// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_onering.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/one-ring.h"

State_OneRing::State_OneRing(const char* InStateName) : State(InStateName)
{

}

void State_OneRing::onStateBegin()
{
    State::onStateBegin();
    
    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)one_ring, sizeof(one_ring));
}

void State_OneRing::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();
    float deltaTime = lastFrameDT.count();

    fireOffset.tick(deltaTime);
    lfoInchwormSpeed.tick(deltaTime);
    lfoNecklaceOuter.speed = remap(0.0f, 1.0f, -inchwormSpeed, inchwormSpeed, lfoInchwormSpeed.evaluate(1.0f));
    lfoNecklaceOuter.tick(deltaTime);
    throbLFO.tick(deltaTime);

    float currentThrobAlpha = throbLFO.evaluate(0.0f);

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        float lfo = lfoNecklaceOuter.evaluate(idx);
        j::HSV color = firePalette.getColor(lfo);
        color.v *= 1.0f - currentThrobAlpha;
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, color);
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        float lfo = lfoNecklaceOuter.evaluate(idx);
        j::HSV color = firePalette.getColor(lfo);
        color.v *= 1.0f - currentThrobAlpha;
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, color);
    }

    for(int i = 0; i < GM.OutfitLEDs->getLength(); ++i)
    {
        j::HSV color = firePalette.getColor(fireOffset.evaluate(i));
        color.v *= 1.0f - currentThrobAlpha;
        GM.OutfitLEDs->setHSV(i, color);
    }
}