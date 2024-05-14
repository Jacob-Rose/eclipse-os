// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_boot.h"

#include <memory>

#include "../../lib/j/jio.h"
#include "../../gm.h"
#include "../../lib/j/jlogging.h"

#include "../../imgs/eclipse.h"

State_Boot::State_Boot(const char* InStateName) : State(InStateName)
{
    sawFillPercentage.width = 14.0f;
    sawFillPercentage.speed = 100.0f;
}

bool State_Boot::isStateComplete() const
{
    return GetStateActiveDuration().count() > 2.0f;
}

void State_Boot::onStateBegin()
{
    State::onStateBegin();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)eclipse, sizeof(eclipse));
}


void State_Boot::addStateToInit(std::weak_ptr<State> stateToInit)
{
    statesToInit.push_back(stateToInit);
}

void State_Boot::tick()
{
    State::tick();

    // lil hacky way, but we want leds to be be fine while we load all out stuff
    if(!bInitializedAllStates)
    {
        for(auto stateItr : statesToInit)
        {
            stateItr.lock()->init();
        }
        bInitializedAllStates = true;
    }

    GameManager& GM = GameManager::get();
    float deltaTime = GetLastFrameDelta().count();

    sawFillPercentage.tick(deltaTime);
    jlog::print(std::to_string(sawFillPercentage.evaluate(0.0f)));

    float currentBootPercentComplete = sawFillPercentage.evaluate(0.0f);

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        float alphaPercent = (float)(idx + 1) / RING_ONE_LENGTH;
        if(currentBootPercentComplete < alphaPercent)
        {
            j::HSV color = j::HSV(currentBootPercentComplete, 200, 200);
            GM.RingLEDs->setHSV(idx, color);
        }
        else
        {
            j::HSV color = j::HSV(0,0,0);
            GM.RingLEDs->setHSV(idx, color);
        }
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        float alphaPercent = (float)(idx + 1) / RING_TWO_LENGTH;
        if(currentBootPercentComplete < alphaPercent)
        {
            j::HSV color = j::HSV(currentBootPercentComplete, 200, 200);
            GM.RingLEDs->setHSV(idx + RING_ONE_LENGTH, color);
        }
        else
        {
            j::HSV color = j::HSV(0,0,0);
            GM.RingLEDs->setHSV(idx + RING_ONE_LENGTH, color);
        }
    }

    for(int idx = 0; idx < ARM_LED_LENGTH; ++idx)
    {
        float alphaPercent = (float)(idx + 1) / ARM_LED_LENGTH;
        if(currentBootPercentComplete < alphaPercent)
        {
            j::HSV color = j::HSV(currentBootPercentComplete, 200, 200);
            GM.OutfitLEDs->setHSV(idx, color);
        }
        else
        {
            j::HSV color = j::HSV(0,0,0);
            GM.OutfitLEDs->setHSV(idx, color);
        }
    }

    for(int idx = 0; idx < WHIP_LED_LENGTH; ++idx)
    {
        float alphaPercent = (float)(idx + 1) / WHIP_LED_LENGTH;
        alphaPercent = 1.0f - alphaPercent;
        if(currentBootPercentComplete < alphaPercent)
        {
            j::HSV color = j::HSV(currentBootPercentComplete, 200, 200);
            GM.OutfitLEDs->setHSV(idx + ARM_LED_LENGTH, color);
        }
        else
        {
            j::HSV color = j::HSV(0,0,0);
            GM.OutfitLEDs->setHSV(idx + ARM_LED_LENGTH, color);
        }
    }
}