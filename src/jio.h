// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <AnimatedGIF.h>

#include <Adafruit_GC9A01A.h>
#include <Adafruit_NeoPixel.h>

#include <memory>
#include <chrono>
#include <ctime>

#include "jcolors.h"

#pragma region DEFINES
// BEGIN PIN + HARDWARE DEFINES
// redefine as needed

#define GREEN_BUTTON_PIN D12
#define RED_BUTTON_PIN D13
#define BLUE_BUTTON_PIN D14

#define RING_LED_PIN D24
#define RING_ONE_LENGTH 12
#define RING_TWO_LENGTH 16
#define RING_LED_LENGTH 28 //RING_ONE_LENGTH + RING_TWO_LENGTH

#define OUTFIT_LED_PIN 6
#define ARM_LED_LENGTH 60
#define WHIP_LED_LENGTH 32
#define OUTFIT_LED_LENGTH 92 //ARM_LED_LENGTH + WHIP_LED_LENGTH

#define SCREEN_DC 3
#define SCREEN_CS 2
#define SCREEN_RST -1
#define SCREEN_MISO D11 // labeled MOSI in docs but actually the SDA on my chip, actually GPIO2 on Feather RP2040, 
#define SCREEN_SCLK D10 // labeled as SCL on my chip

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#pragma endregion

namespace j
{
    class Button
    {
    public:

        void init(uint8_t pin);

        void checkChange(float tick);

        bool isPressed();

    private:
        uint8_t myPin;

        bool bInit = false;
    };

    // screen drawer for drawing pixel art
    // has performance solutions that optimize for pixel art on multiple stages
    class ScreenDrawer
    {
    public:
        ScreenDrawer();

        void setCanvasSize(uint16_t x, uint16_t y);
        void setScreenRef(std::shared_ptr<Adafruit_GC9A01A> inScreenRef);

        // using canvas pixels, lets us scale our performance with our image size
        uint16_t getPixelColor(uint16_t x, uint16_t y);
        void setPixelColor(uint16_t x, uint16_t y, uint16_t color);

        static void GIFDraw_UpscaleScreen(GIFDRAW *pDraw);

    public:
        std::shared_ptr<Adafruit_GC9A01A> ScreenRef;

        // todo add canvas / screen memory for setting up things
        bool bCanvasEnabled = false;
        int16_t xCanvasSize, yCanvasSize;
        std::vector<uint16_t> colors;
    };




    /// @brief HSV Wrapper for Adafruit_Neopixel
    ///
    /// HSV Wrapper for an Adafruit Neopixel strip, allows us to lerp and perform much cleaner calculations 
    /// at the cost of performance, but honestly, def worth it in this case.
    /// 
    /// gives us an easy way to brighten, dim, and blend colors using HSV, which improves accuracy tremendously
    ///
    class HSVStrip
    {
    public:
        HSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType);
        ~HSVStrip();

        HSV getHSV(uint16_t idx) const;
        void setHSV(uint16_t idx, const HSV& hsv);
        void setHSV(uint16_t idx, uint16_t h, uint8_t s, uint8_t v);

        uint8_t getBrightness(uint16_t idx) const;
        void setBrightness(uint16_t idx, uint8_t val);
        
        void show();

        uint16_t getLength() const;

    protected:
        void updateStripPixel(uint16_t idx);

        // gamma correction applied on updateStripPixel
        bool bUsesGammaCorrection = true;

        std::vector<HSV> strip_HSV;

        Adafruit_NeoPixel strip;
    };

    /*
    struct Coordinate
    {
        Coordinate(float x, float y);
        float x;
        float y;
    };

    // same library, responsiblity of user to use additional featureset for 2d specific effects
    class MappedHSVStrip
    {
    public:
        
        MappedHSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType);
        ~MappedHSVStrip();

        Coordinate getCoord(uint16_t idx) const;
        void loadCoords(uint16_t coords[]);

        enum StripMapping
        {
            OneToOne,
            Stretch,
            Repeat
        };

        struct StripInfo
        {
            std::shared_ptr<HSVStrip> strip;
            StripMapping mappingMode;
        };

        const StripInfo& getStripInfo(uint8_t idx) const;
        uint8_t getStripCount() const;
    protected:
        std::vector<Coordinate> coordinates;

        std::vector<StripInfo> strips;
    }
    */
};