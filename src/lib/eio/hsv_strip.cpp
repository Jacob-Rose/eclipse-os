// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "hsv_strip.h"

#include <memory>
#include <algorithm>

#include "../ecore/core.h"
#include "../ecore/logging.h"

using namespace eio;
using namespace ecore::log;

#if USING_NEOPIXEL
HSVStrip::HSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType) : strip(inLedCount, inLedPin, inPixelType)
{
    strip_HSV = std::vector<HSV>(inLedCount);

    strip.begin();
}
#endif

HSVStrip::HSVStrip(uint16_t inLedCount, uint16_t inLedPin) :
#if USING_NEOPIXEL
    HSVStrip(inLedCount, inLedPin, NEO_GRB + NEO_KHZ800)
#endif
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
#if ERROR_CHECKING_ENABLED
    if(idx >= strip_HSV.size())
    {
#if DEBUG_LOGGING_ENABLED
        std::string str = "HSVStrip::setHSV - index out of range: " + std::to_string(idx) + " >= " + std::to_string(strip_HSV.size());
        dbgLog(str.c_str(), Verbosity::Error, Category::Library);
#endif
        return;
    }
#endif
    strip_HSV[idx] = hsv;

    updateStripPixel(idx);
}

void HSVStrip::setHSV(uint16_t idx, float h, uint8_t s, uint8_t v)
{
#if ERROR_CHECKING_ENABLED
    if(idx >= strip_HSV.size())
    {
#if DEBUG_LOGGING_ENABLED
        std::string str = "HSVStrip::setHSV - index out of range: " + std::to_string(idx) + " >= " + std::to_string(strip_HSV.size());
        dbgLog(str.c_str(), Verbosity::Error, Category::Library);
#endif
        return;
    }
#endif

    uint32_t v32 = ((uint32_t)v * 255u) / SCALE_FACTOR;
    strip_HSV[idx].v = static_cast<uint8_t>(std::clamp(v32, static_cast<uint32_t>(0), static_cast<uint32_t>(255)));

    uint32_t s32 = ((uint32_t)s * 255u) / SCALE_FACTOR;
    strip_HSV[idx].s = static_cast<uint8_t>(std::clamp(s32, static_cast<uint32_t>(0), static_cast<uint32_t>(255)));

    h = std::fmod(h, 360.0f);
    h /= 360.0f;
    uint64_t h64 = ((uint64_t)h * static_cast<uint64_t>(UINT16_MAX)) / SCALE_FACTOR;
    strip_HSV[idx].h = static_cast<uint16_t>(std::clamp(h64, static_cast<uint64_t>(0), static_cast<uint64_t>(UINT16_MAX)));

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
#if DEBUG_LOGGING_ENABLED
    dbgLog("HSVStrip::show", Verbosity::VeryVerbose, Category::Library);
#endif
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

HSVStripNode::HSVStripNode(HSVStripSegment *inParentStripSegment, int inStripIdx) : parentStripSegment(inParentStripSegment), stripIdx(inStripIdx) 
{

}

void HSVStripNode::setHSV(const HSV &hsv)
{
    parentStripSegment->getParentStrip()->setHSV(stripIdx, hsv);
}

void HSVStripNode::setHSV(float h, uint8_t s, uint8_t v)
{
    parentStripSegment->getParentStrip()->setHSV(stripIdx, h, s, v);
}

void eio::HSVStripNode::setBuffer(int idx, const HSV &inHSV)
{
    bufferMap[idx] = inHSV;
}

HSV eio::HSVStripNode::getBuffer(int idx) const
{
    auto it = bufferMap.find(idx);
    if(it == bufferMap.end())
    {
#if DEBUG_LOGGING_ENABLED
        std::string str = "HSVStripNode::getBuffer - no buffer found for index: " + std::to_string(idx);
        dbgLog(str.c_str(), Verbosity::Error, Category::Library);
#endif
        return HSV(); // Return a default HSV if not found
    }
    return it->second;
}

void eio::HSVStripNode::clearBuffer(int idx)
{
    bufferMap.erase(idx); // Remove the entry from the buffer map
}

std::vector<std::pair<int, HSV>> eio::HSVStripNode::getBufferMap() const
{
    return std::vector<std::pair<int, HSV>>(bufferMap.begin(), bufferMap.end());
}

eio::HSVStripSegment::HSVStripSegment(HSVStrip* inParentStrip, int inId) : parentStrip(inParentStrip), id(inId)
{
}


void eio::HSVStripSegment::addNode(std::shared_ptr<HSVStripNode> node)
{
    Nodes.push_back(node);
}

void eio::HSVStripSegment::addNodes(std::vector<std::shared_ptr<HSVStripNode>> inNodes)
{
    Nodes.insert(Nodes.end(), inNodes.begin(), inNodes.end());
}


const std::vector<std::shared_ptr<HSVStripNode>> &eio::HSVStripSegment::getNodes() const
{ 
    return Nodes; 
}