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


namespace j
{
    // buttons are self managing, making them able to track their state better
    // TODO add delegate system
    class Button
    {
    public:
        void init(uint8_t pin);

        void tick(float deltaTime);

        // does not directly poll, must use tick to get updated value this frame
        bool isPressed() const { return bLastSeenPressed; }
        float getTimeSinceStateChange() const { return timeSinceStateChanged; }


        bool bHasBeenReleased = true; // hacky implementation done on necklace

        // lets us reset button timers easily
        void resetTimeSinceStateChange();
    private:
        bool pollPressed();

        uint8_t myPin;

        bool bLastSeenPressed = false;

        float timeSinceStateChanged = 0.0f;

        bool bInit = false;
    };

    // nice lil wrapper that can be passed in button refs and 
    // handle tap events

    // TODO
    class ButtonObserver
    {

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

        void renderGif(AnimatedGIF& gif);
        void cancelGifRender();

    public:
        std::shared_ptr<Adafruit_GC9A01A> ScreenRef;

        // todo add canvas / screen memory for setting up things
        bool bCanvasEnabled = false;
        int16_t xCanvasSize, yCanvasSize;
        std::vector<uint16_t> colors;

    private:
        bool bWasCancelled = false;
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
        void setHSV(uint16_t idx, float h, uint8_t s, uint8_t v);

        uint8_t getBrightness(uint16_t idx) const;
        void setBrightness(uint16_t idx, uint8_t val);

        uint8_t getStripBrightness() const;
        void setStripBrightness(uint8_t brightness);
        
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

    // same interface to HSVStrip, responsiblity of user to use additional featureset for 2d specific effects
    // to support dynamic changing of mapping per state as well
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