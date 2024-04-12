// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_hacker.h"

#include "../../lib/j/jio.h"
#include "../../gm.h"
#include "../../lib/j/jlogging.h"

State_Hacker::State_Hacker(const char* InStateName) : State(InStateName)
{

}

void State_Hacker::init()
{
    State::init();
}

void State_Hacker::tickScreen()
{
    State::tickScreen();

    GlobalManager& GM = GlobalManager::get();

    float currentAngleSin = std::sin(currentScreenAngle);
    float currentAngleCos = std::cos(currentScreenAngle);

    int xStart = ((currentAngleSin + 1.0f) * 0.5f) * SCREEN_WIDTH;
    int yStart = 0;

    int xEnd = SCREEN_WIDTH - xStart;
    int yEnd = SCREEN_HEIGHT - yStart;

    GM.Screen->drawLine(xStart, yStart, xEnd, yEnd, GC9A01A_BLACK);

    xStart = 0;
    yStart = ((currentAngleCos + 1.0f) * 0.5f) * SCREEN_WIDTH;

    xEnd = SCREEN_WIDTH - xStart;
    yEnd = SCREEN_HEIGHT - yStart;

    GM.Screen->drawLine(xStart, yStart, xEnd, yEnd, GC9A01A_BLACK);

    float tickTime = lastFrameDT_Screen.count();

    currentScreenAngle += tickTime * screenRotateSpeed;

    currentAngleSin = std::sin(currentScreenAngle);
    currentAngleCos = std::cos(currentScreenAngle);

#if LOGGING_ENABLED
    std::string debugStr;
    debugStr.append("angle: ");
    debugStr.append(String(currentScreenAngle, 5).c_str());
    debugStr.append("sin: ");
    debugStr.append(String(currentAngleSin, 5).c_str());
    debugStr.append("cos: ");
    debugStr.append(String(currentAngleCos, 5).c_str());

    jlog::print(debugStr, Verbosity::Display);
#endif

    xStart = ((currentAngleSin + 1.0f) * 0.5f) * SCREEN_WIDTH;
    yStart = 0;

    xEnd = SCREEN_WIDTH - xStart;
    yEnd = SCREEN_HEIGHT - yStart;

    GM.Screen->drawLine(xStart, yStart, xEnd, yEnd, GC9A01A_CYAN);

    xStart = 0;
    yStart = ((currentAngleCos + 1.0f) * 0.5f) * SCREEN_WIDTH;

    xEnd = SCREEN_WIDTH - xStart;
    yEnd = SCREEN_HEIGHT - yStart;

    GM.Screen->drawLine(xStart, yStart, xEnd, yEnd, GC9A01A_CYAN);
}

void State_Hacker::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    uint16_t randPixel_Rings = std::rand() % RING_LED_LENGTH;
    uint16_t randHue_Rings = std::rand();

    GM.RingLEDs->setHSV(randPixel_Rings, randHue_Rings, 200, 80); // set to 100 when back to hacker

    for(uint16_t ledIdx = 0; ledIdx < RING_LED_LENGTH; ++ledIdx)
    {
        j::HSV color = GM.RingLEDs->getHSV(ledIdx);
        color.v = std::floor(0.9f * color.v);
        GM.RingLEDs->setHSV(ledIdx, color);
    }

    uint16_t randPixel_Outfit = std::rand() % OUTFIT_LED_LENGTH;
    uint16_t randHue_Outfit = std::rand();

    GM.OutfitLEDs->setHSV(randPixel_Outfit, randHue_Outfit, 200, 80); // set to 100 when back to hacker

    for(uint16_t ledIdx = 0; ledIdx < OUTFIT_LED_LENGTH; ++ledIdx)
    {
        j::HSV color = GM.OutfitLEDs->getHSV(ledIdx);
        color.v = std::floor(0.9f * color.v);
        GM.OutfitLEDs->setHSV(ledIdx, color);
    }
}

void State_Hacker::tickLogic()
{
    State::tickLogic();
}