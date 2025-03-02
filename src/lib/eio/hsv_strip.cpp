// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "hsv_strip.h"

#include <memory>

#include "../ecore/logging.h"



using namespace eio;
using namespace std;

/*
#if USING_NEOPIXEL
HSVStrip::HSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType) : HSVStrip(inLedCount, inLedPin)
, strip(inLedCount, inLedPin, inPixelType)
{
    strip_HSV = std::vector<HSV>(inLedCount);

    strip.begin();
}
#endif
*/

HSVStrip::HSVStrip(uint16_t inLedCount, uint16_t inLedPin)
{

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
    strip_HSV[idx] = hsv;

    updateStripPixel(idx);
}

void HSVStrip::setHSV(uint16_t idx, float h, uint8_t s, uint8_t v)
{
    strip_HSV[idx].h = h;
    strip_HSV[idx].s = s;
    strip_HSV[idx].v = v;

    updateStripPixel(idx);
}

uint8_t HSVStrip::getBrightness(uint16_t idx) const
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
#if USING_NEOPIXEL
    uint32_t neoColor = Adafruit_NeoPixel::ColorHSV(strip_HSV[idx].getHueAs16(), strip_HSV[idx].getSatAs8(), strip_HSV[idx].getValAs8());
    if(bUsesGammaCorrection)
    {
        neoColor = Adafruit_NeoPixel::gamma32(neoColor);
    }

    strip.setPixelColor(idx, neoColor);
#endif
}

void HSVStrip::show()
{
#if USING_NEOPIXEL
    strip.show();
#endif
}

uint16_t HSVStrip::getLength() const
{
#if USING_NEOPIXEL
    return strip.numPixels();
#endif
    return 0;
}

uint8_t HSVStrip::getStripBrightness() const
{
#if USING_NEOPIXEL
    return strip.getBrightness();
#endif

    return 0;
}

void HSVStrip::setStripBrightness(uint8_t inBrightness)
{
#if USING_NEOPIXEL
    strip.setBrightness(inBrightness);
#endif
}

void HSVStrip::updateStripPixels()
{
    for(uint16_t idx = 0; idx < getLength(); ++idx)
    {
        updateStripPixel(idx);
    }
}

eio::HSVStripSegment::HSVStripSegment(HSVStrip* inParentStrip) : parentStrip(inParentStrip)
{
}

void eio::HSVStripSegment::addNode(shared_ptr<HSVStripNode> node)
{
    Nodes.push_back(node);
}

void eio::HSVStripSegment::addNodes(vector<shared_ptr<HSVStripNode>> inNodes)
{
    Nodes.insert(Nodes.end(), inNodes.begin(), inNodes.end());
}
