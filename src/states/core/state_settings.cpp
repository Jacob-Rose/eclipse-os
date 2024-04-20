// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_settings.h"

#include "../../gm.h"
#include "../../lib/j/jcolors.h"
#include "../../lib/j/jmath.h"
#include "../../lib/j/jlogging.h"

#include "../../imgs/gears.h"

State_Settings::State_Settings(const char* InStateName) : State(InStateName)
{

}

void State_Settings::init()
{
    State::init();
    gif.begin(LITTLE_ENDIAN_PIXELS);

    gif.open((uint8_t *)gears, sizeof(gears), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_Settings::tickScreen()
{
    State::tickScreen();
    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.renderGif(gif);
}

void State_Settings::tickLEDs()
{
    State::tickLEDs();
    
    GameManager& GM = GameManager::get();

    if(GM.BlueButton->isPressed())
    {
        if(!bBlueButtonSeenPressed)
        {
            bBlueButtonSeenPressed = true;
            EBrightness currentBrightness = GM.getGlobalBrightness();
            currentBrightness = (EBrightness)(((int)currentBrightness + 1) % (int)EBrightness::MAX);
            GM.setGlobalBrightness(currentBrightness);
        }
    }
    else
    {
        bBlueButtonSeenPressed = false;
    }

    for(int idx = 0; idx < RING_LED_LENGTH; ++idx)
    {
        GM.RingLEDs->setHSV(idx, j::HSV(0.0f,0,255));
    }
}

void State_Settings::tickLogic()
{
    State::tickLogic();
}
