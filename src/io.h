// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <memory>
#include <chrono>
#include <ctime>

// BEGIN PIN + HARDWARE DEFINES
// redefine as needed

#define GREEN_BUTTON_PIN D12
#define RED_BUTTON_PIN D13
#define BLUE_BUTTON_PIN D14

#define RING_LED_PIN D24
#define RING_ONE_LENGTH 12
#define RING_TWO_LENGTH 16
#define RING_LENGTH 28 //RING_ONE_LENGTH + RING_TWO_LENGTH

#define BODY_LED_PIN 6
#define ARM_LED_LENGTH 60
#define WHIP_LED_LENGTH 32
#define BODY_LED_LENGTH 92 //ARM_LED_LENGTH + WHIP_LED_LENGTH

#define SCREEN_DC D9
#define SCREEN_CS D10
#define SCREEN_RST D11
#define SCREEN_MISO 2 // labeled MOSI in docs but actually the SDA on my chip, actually GPIO2 on Feather RP2040, 
#define SCREEN_SCLK 3 // labeled as SCL on my chip

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

class Button
{
public:

    void init(uint8_t pin);

    void checkChange(float tick);

private:
    uint8_t myPin;

    bool bInit = false;
};