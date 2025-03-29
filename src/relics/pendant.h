// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "../lib/ecore/core.h"
#include "../lib/eio/relic.h"
#include "../lib/eio/screen_drawer.h"

using namespace ecore;
using namespace eio;

#if 0 //OLD_PENDANT

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


#endif



namespace pendant
{
    // Made in global space, first value must be the index of our whole system
    enum class EPendantSegmentID
    {
        InnerRing = 42,  // the answer to life, the universe, and everything. also a random number
        OuterRing,
        Board
    };

    constexpr int RingLEDPin = 11; //GPIO 11
    constexpr int OutfitLEDPin = 6; // GPIO 6
    constexpr int GreenButtonPin = A3; // GPIO 12
    constexpr int RedButtonPin = A0; // GPIO 13
    constexpr int BlueButtonPin = A1; // GPIO 26
    constexpr int WhiteButtonPin = A2; // GPIO 27
    constexpr int RemoteWhiteButtonPin = 13; // GPIO 9
    constexpr int RemoteBlackButtonPin = 12; // GPIO 8

    constexpr int ScreenDC = 1;
    constexpr int ScreenCS = 0; // GPIO 0
    constexpr int ScreenRST = -1; // GPIO 2
    constexpr int ScreenSDA = 20; // GPIO 19
    constexpr int ScreenSCL = 19; // GPIO 10

    /*
    * @brief PendantIO is the IO class for the pendant relic. It handles the LED strips and buttons.
    *
    * Pendant... well pendant is special. 
    * Its my generic device made for swapping between devices.
    * 
    * Will not initialize OutfitLED strip as that can be done by the outfit that inherits/encapsulates itself.
    */
    class PendantIO : public RelicIO
    {
    public:
        PendantIO();

        virtual void init() override;
        virtual void tick(float deltaTime) override;
        virtual void tick2();
        virtual void cleanup();

    private:
        std::chrono::duration<double> lastFrameDT;
        std::chrono::time_point<std::chrono::system_clock> tickStartTime;

        std::unique_ptr<ScreenDrawer> screenDrawer;
    };

    class PendantCore : public RelicCore
    {
    public:
        PendantCore();

        virtual void init() override;
        virtual void tick(float deltaTime) override;

        // used for screen tick
        virtual void tick2();
    };
}