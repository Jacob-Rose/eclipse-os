// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_serendipity.h"

#include "../../imgs/squid.h"

#include "../../gm.h"


State_Serendipidy::State_Serendipidy(const char* InStateName) : State(InStateName)
{

}

void State_Serendipidy::init()
{
    State::init();

    img.begin(LITTLE_ENDIAN_PIXELS);

    img.open((uint8_t *)squid, sizeof(squid), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_Serendipidy::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    uint16_t randPixel_Rings = std::rand() % RING_LED_LENGTH;
    uint16_t randHue_Rings = std::rand();

    GM.RingLEDs->setHSV(randPixel_Rings, randHue_Rings, 128, 128); // set to 100 when back to hacker

    for(uint16_t ledIdx = 0; ledIdx < RING_LED_LENGTH; ++ledIdx)
    {
        j::HSV color = GM.RingLEDs->getHSV(ledIdx);
        color.v = std::floor(0.98f * color.v);
        GM.RingLEDs->setHSV(ledIdx, color);
    }

    if(GM.bHasOutfitConnected)
    {
        uint16_t randPixel_Outfit = std::rand() % OUTFIT_LED_LENGTH;
        uint16_t randHue_Outfit = std::rand();

        GM.OutfitLEDs->setHSV(randPixel_Outfit, randHue_Outfit, 128, 255); // set to 100 when back to hacker

        for(uint16_t ledIdx = 0; ledIdx < OUTFIT_LED_LENGTH; ++ledIdx)
        {
            j::HSV color = GM.OutfitLEDs->getHSV(ledIdx);
            color.v = std::floor(0.98f * color.v);
            GM.OutfitLEDs->setHSV(ledIdx, color);
        }
    }

    delay(20);
}

void State_Serendipidy::tickScreen()
{
    State::tickScreen();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.renderGif(img);
}