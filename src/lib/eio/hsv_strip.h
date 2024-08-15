// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <vector>

#include <Adafruit_NeoPixel.h>

#include "../ecore/hsv.h"

using namespace ecore;

namespace eio
{
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

        void updateStripPixels();

        std::vector<HSV>& getStripHSV() { return strip_HSV; }
        
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
}