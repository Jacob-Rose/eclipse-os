// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_sleep.h"

#include "../../gm.h"
#include "../../lib/j/jcolors.h"
#include "../../lib/j/jmath.h"
#include "../../lib/j/jlogging.h"

#include "../../imgs/matrix.h"

State_Sleep::State_Sleep(const char* InStateName) : State(InStateName)
{

}

void State_Sleep::init()
{
    State::init();
    
    img.begin(LITTLE_ENDIAN_PIXELS);

    img.open((uint8_t *)matrix, sizeof(matrix), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_Sleep::tick()
{
    State::tick();
    
    GameManager& GM = GameManager::get();

    for(int ledIdx = 0; ledIdx < RING_LED_LENGTH; ++ledIdx)
    {
        GM.RingLEDs->setHSV(ledIdx, 0, 0, 0);
    }

    for(int ledIdx = 0; ledIdx < OUTFIT_LED_LENGTH; ++ledIdx)
    {
        GM.OutfitLEDs->setHSV(ledIdx, 0, 0, 0);
    }

    GM.BoardLED->setHSV(0,j::HSV(0.5f, 120, 60));
}

void State_Sleep::tickScreen()
{
    State::tickScreen();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.renderGif(img);
}