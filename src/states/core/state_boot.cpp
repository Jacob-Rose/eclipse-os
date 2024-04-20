// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_boot.h"

#include <memory>

#include "../../lib/j/jio.h"
#include "../../gm.h"
#include "../../lib/j/jlogging.h"

#include "../../imgs/flicker-stars.h"

State_Boot::State_Boot(const char* InStateName) : State(InStateName)
{

}

void State_Boot::tickLEDs()
{
    State::tickLEDs();

    GameManager& GM = GameManager::get();

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

void State_Boot::init()
{
    State::init();

    gif.begin(LITTLE_ENDIAN_PIXELS);

    gif.open((uint8_t *)flicker_stars, sizeof(flicker_stars), j::ScreenDrawer::GIFDraw_UpscaleScreen);
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

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.renderGif(gif);

    // lil hacky way, but we want leds to be be fine while we load all out stuff
    if(!bInitializedAllStates)
    {
        for(auto stateItr : statesToInit)
        {
            stateItr.lock()->init();
        }
        bInitializedAllStates = true;
    }
}