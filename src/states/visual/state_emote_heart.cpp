// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_emote_heart.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/kirby-32-transparency.h"

State_Emote_Heart::State_Emote_Heart(const char* InStateName) : State(InStateName)
{

}

void State_Emote_Heart::init()
{
    State::init();

    HeartGif.begin(LITTLE_ENDIAN_PIXELS);

    bLoadedHeart = HeartGif.open((uint8_t *)kirby_32_transparency, sizeof(kirby_32_transparency), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_Emote_Heart::tickScreen()
{
    State::tickScreen();

    GlobalManager& GM = GlobalManager::get();

    GM.Screen->startWrite();
    int playFrameResult = HeartGif.playFrame(true, NULL, &GM.ScreenDrawer);
    GM.Screen->endWrite();
}

void State_Emote_Heart::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    float tickTime = lastFrameDT_Screen.count();

    float pulseTime = (std::sin(GetStateActiveDuration().count() * 3.0f) + 1.0f) / 2;

    pulseTime *= 128;

    int pulseTimeByte = std::floor(pulseTime);
    
    for(int ledIdx = 0; ledIdx < RING_LED_LENGTH; ++ledIdx)
    {
        GM.RingLEDs->setHSV(ledIdx, 0xf812, 200, pulseTimeByte);
    }
}

void State_Emote_Heart::tickLogic()
{
    State::tickLogic();

    GlobalManager& GM = GlobalManager::get();
}
