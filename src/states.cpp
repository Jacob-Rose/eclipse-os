// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "states.h"

#include "io.h"
#include "gm.h"
#include "logging.h"

#include <SPI.h>
#include "Adafruit_GFX.h"
#include <AnimatedGIF.h>

#include "imgs/homer_tiny.h"
#include "imgs/earth_128x128.h"

void GameState::initSingleton()
{
    if(singleton == nullptr)
    {
        singleton = new GameState();
        singleton->init();
    } 
}

void GameState::cleanupSingleton()
{
    if(singleton != nullptr)
    {
        singleton->cleanup();
        delete singleton;
        singleton = nullptr;
    }
}

void GameState::init()
{
}

void GameState::cleanup()
{
}

State::State(const char* InStateName)
{
    stateName = InStateName;
}


void State::init()
{
#if LOGGING_ENABLED
    std::string tickMsg = "init state: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::cleanup()
{
#if LOGGING_ENABLED
    std::string tickMsg = "cleanup state: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::stateActivated()
{
    activationTime = std::chrono::system_clock::now();
}

void State::stateDeactivated()
{
    
}

void State::tickLEDs()
{
#if LOGGING_ENABLED
    std::string tickMsg = "ticking leds: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::VeryVerbose, Category::OnTick | Category::StateInfo);
#endif
}

void State::tickScreen()
{
#if LOGGING_ENABLED
    std::string tickMsg = "ticking screen: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::VeryVerbose, Category::OnTick | Category::StateInfo);
#endif
}

void State::tickLogic()
{
#if LOGGING_ENABLED
    std::string tickMsg = "ticking logic: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::Verbose, Category::OnTick | Category::StateInfo);
#endif
}

std::chrono::duration<float> State::GetStateActiveDuration() const
{
    std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
    std::chrono::duration<float> timeDiff = currentTime - activationTime;
    return timeDiff;
}

State_Boot::State_Boot(const char* InStateName) : State(InStateName)
{

}

void State_Boot::tickLEDs()
{
    State::tickLEDs();

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

    /*
    HeartGif.begin(LITTLE_ENDIAN_PIXELS);

    bLoadedHeart = HeartGif.open((uint8_t *)earth_128x128, sizeof(earth_128x128), GIFDraw_Necklace);
    if(bLoadedHeart)
    {
        Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", HeartGif.getCanvasWidth(), HeartGif.getCanvasHeight());
    }
    else
    {
        Serial.printf("Unsuccessfully opened GIF");
    }
    */
}

void State_Heartbeat::tickScreen()
{
    GlobalManager& GM = GlobalManager::get();

    //int playFrameResult = HeartGif.playFrame(false, NULL);

    State::tickScreen();
}

void State_Heartbeat::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();
    
    for(int ledIdx = 0; ledIdx < RING_LENGTH; ++ledIdx)
    {
        GM.RingLEDs[ledIdx].setRGB(60,0,0);
    }

    /*
    for(int ledIdx = 0; ledIdx < RING_LENGTH; ++ledIdx)
    {
        GM.RingLEDs[ledIdx].nscale8(200);
    }
    */
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

    GM.Screen->drawLine(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, GC9A01A_CYAN);
    GM.Screen->drawLine(SCREEN_WIDTH/2, 0, SCREEN_WIDTH/2, SCREEN_HEIGHT, GC9A01A_CYAN);
    GM.Screen->drawLine(0, SCREEN_HEIGHT, SCREEN_WIDTH, 0, GC9A01A_CYAN);
    GM.Screen->drawLine(0, SCREEN_HEIGHT/2, SCREEN_WIDTH, SCREEN_HEIGHT/2, GC9A01A_CYAN);
}

void State_Hacker::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    int randPixel = std::rand() % RING_LENGTH;

    GM.RingLEDs[randPixel].setRGB(0,200,0);

    for(int ledIdx = 0; ledIdx < RING_LENGTH; ++ledIdx)
    {
        GM.RingLEDs[ledIdx].nscale8(200);
    }
}

void State_Hacker::tickLogic()
{
    State::tickLogic();
}
