// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "lib/j/jio.h"

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

    GlobalManager();

    void init();
    void cleanup();

    uint8_t getGlobalBrightness() const;
    void setGlobalBrightness(uint8_t newBrightness);

    std::unique_ptr<j::Button> GreenButton;
    std::unique_ptr<j::Button> RedButton;
    std::unique_ptr<j::Button> BlueButton;
    std::unique_ptr<j::Button> WhiteButton;

    std::unique_ptr<j::Button> RemoteWhiteButton;
    std::unique_ptr<j::Button> RemoteBlackButton;

    std::unique_ptr<j::HSVStrip> RingLEDs;
    std::unique_ptr<j::HSVStrip> OutfitLEDs;
    std::unique_ptr<j::HSVStrip> GlassesLEDs;
    std::unique_ptr<j::HSVStrip> BoardLED;

    std::shared_ptr<Adafruit_GC9A01A> Screen;
    j::ScreenDrawer ScreenDrawer;

    // USER SETTINGS
public:
    bool bHasOutfitConnected = true;

    uint8_t globalBrightness = 255;
};