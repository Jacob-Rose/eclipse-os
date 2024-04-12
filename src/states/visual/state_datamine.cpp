// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_datamine.h"

#include "../../gm.h"
#include "../../lib/j/jcolors.h"


State_Datamine::State_Datamine(const char* InStateName) : State(InStateName)
{

}

void State_Datamine::tickScreen()
{
    State::tickScreen();
}

void State_Datamine::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    float deltaTime = GetLastFrameDelta().count();

    lfoArm.tick(deltaTime);
    lfoNecklaceInner.tick(deltaTime);
    lfoNecklaceOuter.tick(deltaTime);
    lfoWhip.tick(deltaTime);

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        //auto easingFunction = getEasingFunction( EaseInOutSine );
        float lfo = lfoNecklaceInner.evaluate(idx);
        //lfo = easingFunction(lfo);
        float pixelBrightness = lfo;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);
        GM.RingLEDs->setHSV(idx, j::HSV(1000,255U,j::GammaBrightnessCorrection[brightnessByte]));
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        //auto easingFunction = getEasingFunction( EaseInOutSine );
        float lfo = lfoNecklaceOuter.evaluate(idx);
        //lfo = easingFunction(lfo);
        float pixelBrightness = lfo;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, j::HSV(1000,255U,j::GammaBrightnessCorrection[brightnessByte]));
    }

    for(int idx = 0; idx < ARM_LED_LENGTH; ++idx)
    {
        float lfo2 = lfoArm.evaluate(idx);
        float pixelBrightness = lfo2;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);
        GM.OutfitLEDs->setHSV(idx, j::HSV(1,255U,brightnessByte));
    }

    for(int idx = 0; idx < WHIP_LED_LENGTH; ++idx)
    {
        //auto easingFunction = getEasingFunction( EaseInOutSine );
        float lfo = lfoWhip.evaluate(idx);
        //lfo = easingFunction(idx);
        float pixelBrightness = lfo;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);
        GM.OutfitLEDs->setHSV(idx + ARM_LED_LENGTH, j::HSV(20000,255U,brightnessByte));
    }
}

void State_Datamine::tickLogic()
{
    State::tickLogic();
}

void State_Datamine::onStateBegin()
{
    State::onStateBegin();
}