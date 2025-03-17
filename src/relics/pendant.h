// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "../lib/eio/relic.h"

using namespace ecore;
using namespace eio;
using namespace std;

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

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

enum class EPendantStripID
{
    Outfit,
    Ring,
    Board
};


class PendantIO : public RelicIO
{
public:
    PendantIO();

    virtual void init();
    virtual void cleanup();

};

class PendantCore : public RelicCore
{

};