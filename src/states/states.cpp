// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "states.h"

#include <SPI.h>
#include "Adafruit_GFX.h"
#include <AnimatedGIF.h>

#include <memory>

#include "../io.h"
#include "../gm.h"
#include "../logging.h"

#include "../imgs/squid.h"
#include "../imgs/amongus-dancing.h"
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


State_Off::State_Off(const char* InStateName) : State(InStateName)
{

}

void State_Off::tickScreen()
{
    State::tickScreen();
}

void State_Off::tickLEDs()
{
    State::tickLEDs();
}

void State_Off::tickLogic()
{
    State::tickLogic();
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

    GM.Screen->startWrite();
    int playFrameResult = HeartGif.playFrame(true, NULL, &GM.ScreenDrawer);
    GM.Screen->endWrite();
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
        GM.RingLEDs->setHSV(ledIdx, 0, 200, pulseTimeByte);
    }
}

void State_Heartbeat::tickLogic()
{
    State::tickLogic();

    GlobalManager& GM = GlobalManager::get();

    jlog::print(std::to_string(GM.GreenButton->isPressed()));
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

    if ( img.open((uint8_t *)kirby_32_transparency, sizeof(kirby_32_transparency), j::ScreenDrawer::GIFDraw_UpscaleScreen) )
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
    GM.ScreenDrawer.setCanvasSize(img.getCanvasWidth(), img.getCanvasHeight());
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
