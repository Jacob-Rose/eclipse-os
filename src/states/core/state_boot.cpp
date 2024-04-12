// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_boot.h"

#include <memory>

#include "../../lib/j/jio.h"
#include "../../gm.h"
#include "../../lib/j/jlogging.h"

State_Boot::State_Boot(const char* InStateName) : State(InStateName)
{

}

void State_Boot::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    int currentLED = currentRotation * RING_TWO_LENGTH;

    GM.RingLEDs->setHSV(currentLED + RING_ONE_LENGTH, currentHue, 200, 100);

    float ringTwoAlpha = (float)currentLED / RING_TWO_LENGTH;

    uint16_t ringTwoLEDIdx = (ringTwoAlpha * RING_ONE_LENGTH);

    GM.RingLEDs->setHSV(ringTwoLEDIdx, currentHue, 200, 100);

    for(int ledIdx = 0; ledIdx < RING_LED_LENGTH; ++ledIdx)
    {
        uint8_t brightness = GM.RingLEDs->getBrightness(ledIdx);
        brightness *= 0.98f;
        GM.RingLEDs->setBrightness(ledIdx, brightness);
    }

    currentHue += 32;

    if(currentLED >= RING_TWO_LENGTH)
    {
        currentLED = 0;
    }
}

void State_Boot::tickLogic()
{
    State::tickLogic();

    currentRotation += rotationSpeed * (lastFrameDT_Logic.count() + lastFrameDT_LED.count());
    currentRotation = std::fmod(currentRotation, 1.0f); //clamp it to 1
}

void State_Boot::addStateToInit(std::weak_ptr<State> stateToInit)
{
    statesToInit.push_back(stateToInit);
}

void State_Boot::tickScreen()
{
    State::tickScreen();

    // lil hacky way, but we want leds to be be fine while we load all out stuff
    if(!bInitializedAllStates)
    {
        GlobalManager& GM = GlobalManager::get();
        GM.Screen->fillRect(0,0, GM.Screen->width(), GM.Screen->height(), 0);
        for(auto stateItr : statesToInit)
        {
            stateItr.lock()->init();
        }
        bInitializedAllStates = true;
    }
}