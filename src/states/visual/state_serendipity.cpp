// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_serendipity.h"

#include "../../imgs/squid-117.h"

#include "../../gm.h"

void* allocateBuffer(uint32_t iSize) { return malloc(iSize); }

uint8_t pTurbo[TURBO_BUFFER_SIZE + 256 + (240*240)];
uint8_t frameBuffer[(128*128)];

State_Serendipidy::State_Serendipidy(const char* InStateName) : State(InStateName)
{

}

void State_Serendipidy::init()
{
    State::init();

    img.begin(LITTLE_ENDIAN_PIXELS);

    if ( img.open((uint8_t *)squid_117, sizeof(squid_117), j::ScreenDrawer::GIFDraw_UpscaleScreen) )
    {
        //img.setDrawType(GIF_DRAW_COOKED);
        //img.allocTurboBuf(allocateBuffer);
        img.allocFrameBuf(allocateBuffer);
    }
}

void State_Serendipidy::tickScreen()
{
    State::tickScreen();

    GlobalManager& GM = GlobalManager::get();

    GM.Screen->startWrite();
    int playFrameResult = img.playFrame(true, NULL, &GM.ScreenDrawer);
    GM.Screen->endWrite();
}

void State_Serendipidy::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

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

        GM.OutfitLEDs->setHSV(randPixel_Outfit, randHue_Outfit, 128, 128); // set to 100 when back to hacker

        for(uint16_t ledIdx = 0; ledIdx < OUTFIT_LED_LENGTH; ++ledIdx)
        {
            j::HSV color = GM.OutfitLEDs->getHSV(ledIdx);
            color.v = std::floor(0.98f * color.v);
            GM.OutfitLEDs->setHSV(ledIdx, color);
        }
    }

    delay(20);
}

void State_Serendipidy::tickLogic()
{
    State::tickLogic();
}