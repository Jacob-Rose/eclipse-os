// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "hsv_strip.h"

#include "../ecore/logging.h"

using namespace eio;

HSVStrip::HSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType) : strip(inLedCount, inLedPin, inPixelType)
{
    strip_HSV = std::vector<HSV>(inLedCount);

    strip.begin();
}

HSVStrip::~HSVStrip()
{
}

HSV HSVStrip::getHSV(uint16_t idx) const
{
    return strip_HSV[idx];
}

void HSVStrip::setHSV(uint16_t idx, const HSV& hsv)
{
    strip_HSV[idx].h = hsv.h;
    strip_HSV[idx].s = hsv.s;
    strip_HSV[idx].v = hsv.v;

    updateStripPixel(idx);
}

void HSVStrip::setHSV(uint16_t idx, float h, uint8_t s, uint8_t v)
{
    strip_HSV[idx].h = h;
    strip_HSV[idx].s = s;
    strip_HSV[idx].v = v;

    updateStripPixel(idx);
}

__UINT8_TYPE__ HSVStrip::getBrightness(uint16_t idx) const
{
    return strip_HSV[idx].v;
}

void HSVStrip::setBrightness(uint16_t idx, uint8_t val)
{
    strip_HSV[idx].v = val;

    updateStripPixel(idx);
}

void HSVStrip::updateStripPixel(uint16_t idx)
{
    uint32_t neoColor = Adafruit_NeoPixel::ColorHSV(strip_HSV[idx].getHueAs16(), strip_HSV[idx].getSatAs8(), strip_HSV[idx].getValAs8());
    if(bUsesGammaCorrection)
    {
        neoColor = Adafruit_NeoPixel::gamma32(neoColor);
    }

    strip.setPixelColor(idx, neoColor);
}

void HSVStrip::show()
{
    strip.show();
}

uint16_t HSVStrip::getLength() const
{
    return strip.numPixels();
}

uint8_t HSVStrip::getStripBrightness() const
{
    return strip.getBrightness();
}

void HSVStrip::setStripBrightness(uint8_t inBrightness)
{
    strip.setBrightness(inBrightness);
}

void HSVStrip::updateStripPixels()
{
    for(uint16_t idx = 0; idx < getLength(); ++idx)
    {
        updateStripPixel(idx);
    }
}
