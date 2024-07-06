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

#pragma region DEFINES

#define IS_V1_MAPPINGS

#ifdef IS_V1_MAPPINGS

// redefine as needed
#define GREEN_BUTTON_PIN D12 // GPIO 12
#define RED_BUTTON_PIN D13 // GPIO 13
#define BLUE_BUTTON_PIN A2 // GPIO 26
#define WHITE_BUTTON_PIN A3 // GPIO 27
#define REMOTE_WHITE_BUTTON_PIN -1 // GPIO 28
#define REMOTE_BLACK_BUTTON_PIN -1 // GPIO 29

#define RING_LED_PIN D24 // GPIO 24
#define OUTFIT_LED_PIN 6 // GPIO 6

#define GLASSES_LED_PIN A0 // GPIO 0
#define SCREEN_DC 3
#define SCREEN_CS 2
#define SCREEN_RST -1
#define SCREEN_SDA D11 // labeled MOSI in docs but actually the SDA on my chip, actually GPIO2 on Feather RP2040, 
#define SCREEN_SCL D10 // labeled as SCL on my chip

#else

// redefine as needed
#define GREEN_BUTTON_PIN A3 // GPIO 13
#define RED_BUTTON_PIN A0 // GPIO 12
#define BLUE_BUTTON_PIN A1 // GPIO 11
#define WHITE_BUTTON_PIN A2 // GPIO 10
#define REMOTE_WHITE_BUTTON_PIN 13 // GPIO 9
#define REMOTE_BLACK_BUTTON_PIN 12 // GPIO 8

#define RING_LED_PIN 11 // GPIO 24
#define OUTFIT_LED_PIN 7 // GPIO 6
#define GLASSES_LED_PIN 10 // GPIO 0 // NOTWORKING

#define SCREEN_DC 1
#define SCREEN_CS 0
#define SCREEN_RST -1
#define SCREEN_SDA 20 // labeled MOSI in docs but actually the SDA on my chip, actually GPIO2 on Feather RP2040, 
#define SCREEN_SCL 19 // labeled as SCL on my chip, but is sclk

#endif

#define RING_ONE_LENGTH 12
#define RING_TWO_LENGTH 16
// RING_ONE_LENGTH + RING_TWO_LENGTH
#define RING_LED_LENGTH 28 


#define ARM_LED_LENGTH 50
#define WHIP_LED_LENGTH 60
// ARM_LED_LENGTH + WHIP_LED_LENGTH
#define OUTFIT_LED_LENGTH 110 


#define GLASSES_LED_LENGTH 2



#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#pragma endregion


enum class EBrightness
{
    NIGHTTRIP,
    MIN,
    MED,
    HIGH,
    BLINDING,
    MAX,
    COUNT 
};

uint8_t getEBrightnessAsByte(EBrightness inBrightness);

class GameManager
{
public:
    static GameManager* singleton;
    static void initSingleton();
    static void cleanupSingleton();
    static GameManager& get() { return *singleton; }

    GameManager();

    void init();
    void cleanup();

    void tick(float deltaTime);
    void showLeds();

    
    EBrightness getGlobalBrightness() const;
    void setGlobalBrightness(EBrightness newBrightness);

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

    bool bIsWhipAttached = true;

    EBrightness currentBrightness;
};