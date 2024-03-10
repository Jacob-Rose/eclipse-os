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


void GlobalManager::init()
{
    GreenButton = std::make_unique<Button>();
    GreenButton->init(GREEN_BUTTON_PIN);

    RedButton = std::make_unique<Button>();
    RedButton->init(RED_BUTTON_PIN);

    BlueButton = std::make_unique<Button>();
    BlueButton->init(BLUE_BUTTON_PIN);

    Screen = std::make_unique<Adafruit_GC9A01A>(SCREEN_CS, SCREEN_DC, SCREEN_MISO, SCREEN_SCLK, SCREEN_RST);
    Screen->begin();
    Screen->setRotation(1);
    Screen->fillScreen(GC9A01A_BLACK);

    FastLED.addLeds<NEOPIXEL, RING_LED_PIN>(RingLEDs, RING_LENGTH);
    FastLED.addLeds<NEOPIXEL, BODY_LED_PIN>(OutfitLEDs, BODY_LED_LENGTH);
    FastLED.addLeds<NEOPIXEL, PIN_NEOPIXEL>(BoardLED, 1, 0);
}

void GlobalManager::cleanup()
{
    GreenButton.reset();
    RedButton.reset();
    BlueButton.reset();

    Screen.reset();
}

void GIFDraw_Necklace(GIFDRAW *pDraw)
{

}