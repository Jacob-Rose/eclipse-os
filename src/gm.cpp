// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "gm.h"

GlobalManager* GlobalManager::singleton = nullptr;

void GlobalManager::initSingleton()
{
    if(singleton == nullptr)
    {
        singleton = new GlobalManager();
        singleton->init();
    } 
}

void GlobalManager::cleanupSingleton()
{
    if(singleton != nullptr)
    {
        singleton->cleanup();
        delete singleton;
        singleton = nullptr;
    }
}

GlobalManager::GlobalManager()
{

}


void GlobalManager::init()
{
    GreenButton = std::make_unique<j::Button>();
    GreenButton->init(GREEN_BUTTON_PIN);

    RedButton = std::make_unique<j::Button>();
    RedButton->init(RED_BUTTON_PIN);

    BlueButton = std::make_unique<j::Button>();
    BlueButton->init(BLUE_BUTTON_PIN);

    Screen = std::make_shared<Adafruit_GC9A01A>(SCREEN_CS, SCREEN_DC, SCREEN_MISO, SCREEN_SCLK, SCREEN_RST);
    Screen->begin();
    Screen->setRotation(0);

    ScreenDrawer.setScreenRef(Screen);

    RingLEDs = std::make_unique<j::HSVStrip>(RING_LED_LENGTH, RING_LED_PIN, NEO_GRB + NEO_KHZ800);
    OutfitLEDs = std::make_unique<j::HSVStrip>(OUTFIT_LED_LENGTH, OUTFIT_LED_PIN, NEO_GRB + NEO_KHZ800);
    BoardLED = std::make_unique<j::HSVStrip>(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
}

void GlobalManager::cleanup()
{
    GreenButton.reset();
    RedButton.reset();
    BlueButton.reset();

    RingLEDs.reset();
    OutfitLEDs.reset();


    Screen.reset();
}