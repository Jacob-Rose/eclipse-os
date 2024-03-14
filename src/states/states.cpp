// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "states.h"

#include <SPI.h>
#include "Adafruit_GFX.h"
#include <AnimatedGIF.h>

#include "../io.h"
#include "../gm.h"
#include "../logging.h"

#include "../imgs/squid.h"
#include "../imgs/homer_tiny.h"
#include "../imgs/kirby-32-transparency.h"
#include "../imgs/valentines3q.h"

/// ---------------------
///
/// BEGIN PER STATE DEFS
///
/// ---------------------

State_Boot::State_Boot(const char* InStateName) : State(InStateName)
{

}

void State_Boot::tickLEDs()
{
    State::tickLEDs();

    /*

    GlobalManager& GM = GlobalManager::get();

    GM.BoardLED[0].setHSV(currentHue, 200,50);

    GM.RingLEDs[currentLED].setHSV(currentHue, 200, 50);

    for(int ledIdx = 0; ledIdx < RING_LENGTH; ++ledIdx)
    {
        GM.RingLEDs[ledIdx].nscale8(200);
    }

    currentHue++;
    currentLED++;
    if(currentLED >= RING_LENGTH)
    {
        currentLED = 0;
    }
    */
}

void State_Boot::tickLogic()
{
    State::tickLogic();
}

void State_Boot::tickScreen()
{
    State::tickScreen();
}

State_Emote::State_Emote(const char* InStateName) : State(InStateName)
{

}

void State_Emote::tickLEDs()
{
    State::tickLEDs();

    /*
    std::unique_ptr<Adafruit_NeoPixel>& BoardLED = GlobalManager::get()->BoardLED;
    BoardLED->setPixelColor(0,10,50,0, 0);
    BoardLED->show();

    std::unique_ptr<Adafruit_NeoPixel>& RingLEDs = GlobalManager::get()->RingLEDs;
    for(int i = 0; i < RingLEDs->numPixels(); ++i)
    {
        RingLEDs->setPixelColor(i, 10,100,50);
    }
    RingLEDs->show();
    */
}

void State_Emote::tickLogic()
{
    State::tickLogic();
}

void State_Emote::tickScreen()
{
    State::tickScreen();
}

State_Heartbeat::State_Heartbeat(const char* InStateName) : State(InStateName)
{

}

void State_Heartbeat::init()
{
    State::init();

    HeartGif.begin(LITTLE_ENDIAN_PIXELS);

    bLoadedHeart = HeartGif.open((uint8_t *)valentines3q, sizeof(valentines3q), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_Heartbeat::tickScreen()
{
    State::tickScreen();

    GlobalManager& GM = GlobalManager::get();

    int playFrameResult = HeartGif.playFrame(true, NULL, &GM.ScreenDrawer);
}

void State_Heartbeat::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    float tickTime = lastFrameDT_Screen.count();

    float pulseTime = (std::sin(GetStateActiveDuration().count() * 3.0f) + 1.0f) / 2;

    pulseTime *= 128;

    int pulseTimeByte = std::floor(pulseTime);
    
    for(int ledIdx = 0; ledIdx < RING_LED_LENGTH; ++ledIdx)
    {
        GM.RingLEDs->setPixel(ledIdx, 0, 200, pulseTimeByte);
    }

    GM.RingLEDs->show();
}

void State_Heartbeat::tickLogic()
{
    State::tickLogic();
}


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

    GM.RingLEDs->setPixel(randPixel_Rings, randHue_Rings, 200, 80); // set to 100 when back to hacker

    for(uint16_t ledIdx = 0; ledIdx < RING_LED_LENGTH; ++ledIdx)
    {
        j::HSV color = GM.RingLEDs->getPixel(ledIdx);
        color.v = std::floor(0.9f * color.v);
        GM.RingLEDs->setPixel(ledIdx, color);
    }

    uint16_t randPixel_Outfit = std::rand() % OUTFIT_LED_LENGTH;
    uint16_t randHue_Outfit = std::rand();

    GM.OutfitLEDs->setPixel(randPixel_Outfit, randHue_Outfit, 200, 80); // set to 100 when back to hacker

    for(uint16_t ledIdx = 0; ledIdx < OUTFIT_LED_LENGTH; ++ledIdx)
    {
        j::HSV color = GM.OutfitLEDs->getPixel(ledIdx);
        color.v = std::floor(0.9f * color.v);
        GM.OutfitLEDs->setPixel(ledIdx, color);
    }

    GM.RingLEDs->show();
    GM.OutfitLEDs->show();

    delay(20);
}

void State_Hacker::tickLogic()
{
    State::tickLogic();
}


State_Serendipidy::State_Serendipidy(const char* InStateName) : State(InStateName)
{

}

void State_Serendipidy::init()
{
    State::init();

    img.begin(LITTLE_ENDIAN_PIXELS);

    img.open((uint8_t *)kirby_32_transparency, sizeof(kirby_32_transparency), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_Serendipidy::tickScreen()
{
    State::tickScreen();

    GlobalManager& GM = GlobalManager::get();

    int playFrameResult = img.playFrame(true, NULL, &GM.ScreenDrawer);
}

void State_Serendipidy::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    uint16_t randPixel_Rings = std::rand() % RING_LED_LENGTH;
    uint16_t randHue_Rings = std::rand();

    GM.RingLEDs->setPixel(randPixel_Rings, randHue_Rings, 128, 128); // set to 100 when back to hacker

    for(uint16_t ledIdx = 0; ledIdx < RING_LED_LENGTH; ++ledIdx)
    {
        j::HSV color = GM.RingLEDs->getPixel(ledIdx);
        color.v = std::floor(0.9f * color.v);
        GM.RingLEDs->setPixel(ledIdx, color);
    }

    uint16_t randPixel_Outfit = std::rand() % OUTFIT_LED_LENGTH;
    uint16_t randHue_Outfit = std::rand();

    GM.OutfitLEDs->setPixel(randPixel_Outfit, randHue_Outfit, 128, 128); // set to 100 when back to hacker

    for(uint16_t ledIdx = 0; ledIdx < OUTFIT_LED_LENGTH; ++ledIdx)
    {
        j::HSV color = GM.OutfitLEDs->getPixel(ledIdx);
        color.v = std::floor(0.9f * color.v);
        GM.OutfitLEDs->setPixel(ledIdx, color);
    }

    GM.RingLEDs->show();
    GM.OutfitLEDs->show();

    delay(20);
}

void State_Serendipidy::tickLogic()
{
    State::tickLogic();
}


State_Settings::State_Settings(const char* InStateName) : State(InStateName)
{

}

void State_Settings::init()
{
    State::init();
}

void State_Settings::tickScreen()
{
    State::tickScreen();
}

void State_Settings::tickLEDs()
{
    State::tickLEDs();
}

void State_Settings::tickLogic()
{
    State::tickLogic();
}
