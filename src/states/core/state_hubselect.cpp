// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_hubselect.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include <AnimatedGIF.h>

#include "../../imgs/eclipse.h"

State_HubSelect::State_HubSelect(const char* InStateName) : State(InStateName)
{
}

void State_HubSelect::onStateBegin()
{
    State::onStateBegin();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)eclipse, sizeof(eclipse));
}

void State_HubSelect::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    float deltaTime = GetLastFrameDelta().count();

    lfoNecklaceOuter.tick(deltaTime);

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        GM.RingLEDs->setHSV(idx, j::HSV(0,0,0));
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        float lfo = lfoNecklaceOuter.evaluate(idx);
        j::HSV color = palette.getColor(lfo);
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, color);
    }

    for(int idx = 0; idx < ARM_LED_LENGTH; ++idx)
    {
        float lfo = lfoNecklaceOuter.evaluate(idx);
        j::HSV color = palette.getColor(lfo);
        GM.OutfitLEDs->setHSV(idx, color);
    }

    GM.BoardLED->setHSV(0,j::HSV(0.0f, 0.0f, 0.0f));
}