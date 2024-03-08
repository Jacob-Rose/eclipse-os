// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "FastLED.h"
#include "io.h"

#include "Adafruit_GC9A01A.h"
#include "AnimatedGIF.h"

#include <memory>
#include <ctime>
#include <chrono>

class GlobalManager
{
public:
    static GlobalManager* singleton;
    static void initSingleton();
    static void cleanupSingleton();
    static GlobalManager& get() { return *singleton; }

    void init();
    void cleanup();

    std::unique_ptr<Button> GreenButton;
    std::unique_ptr<Button> RedButton;
    std::unique_ptr<Button> BlueButton;

    CRGB RingLEDs[RING_LENGTH];
    CRGB OutfitLEDs[BODY_LED_LENGTH];
    CRGB BoardLED[1];

    std::unique_ptr<Adafruit_GC9A01A> Screen;

    //std::chrono::duration dtLEDs;
    //std::chrono::duration dtScreen;
    //std::chrono::duration dtLogic;

    //std::time_t ledTickStart;
    //std::time_t screenTickStart;
    //std::time_t logicTickStart; 
};

// Draw a line of image directly on the LCD
void GIFDraw_Necklace(GIFDRAW *pDraw);