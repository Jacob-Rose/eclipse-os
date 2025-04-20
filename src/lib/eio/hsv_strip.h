// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <vector>
#include <memory>
#include <map>

#include "../ecore/core.h"

#define USING_NEOPIXEL USE_ARDUINO && true

#if USING_NEOPIXEL
#include <Adafruit_NeoPixel.h>
#endif

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
#if USING_NEOPIXEL
        HSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType);
#endif
        HSVStrip(uint16_t inLedCount, uint16_t inLedPin);
        ~HSVStrip();

        HSV getHSV(uint16_t idx) const;
        void setHSV(uint16_t idx, const HSV& hsv);
        void setHSV(uint16_t idx, float h, uint8_t s, uint8_t v);

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
#if USING_NEOPIXEL
        Adafruit_NeoPixel strip;
#endif
    };


    // see HSVStripNode:: 
    enum class StripNodeType
    {
        ROOT,
        MAPPED1D,
        MAPPED2D,
        // YOU CAN EXTEND WITH CASTING LOCALLY HERE
    };

    class HSVStripNode
    {
    public:
        HSVStripNode(class HSVStripSegment* inParentStrip, int inStripIdx);
        // we need to provide a virtual destructor so we can delete derived classes safely;
        virtual ~HSVStripNode() = default;
        
        void setHSV(const HSV& hsv);
        void setHSV(float h, uint8_t s, uint8_t v);

        void setBuffer(int idx, const HSV& inHSV);
        HSV getBuffer(int idx) const;
        void clearBuffer(int idx);
        // get all the buffers in a map pair key list
        std::vector<std::pair<int, HSV>> getBufferMap() const;

        // since we dont support RTTI (nor do I want to due to perf concerns), we need to provide a way to check if a node is supported
        virtual StripNodeType GetStripNodeType() const { return StripNodeType::ROOT; }

    private:
        class HSVStripSegment* parentStripSegment;
        int stripIdx{0}; // led index on the strip

        std::map<int, HSV> bufferMap;

    public:
        int getStripIdx() const { return stripIdx; }
        class HSVStripSegment* getStripSegment() const { return parentStripSegment; }
    };


    class HSVStripSegment
    {
    public:
        HSVStripSegment(HSVStrip* parent, int inId);

        void addNode(std::shared_ptr<HSVStripNode> node);
        void addNodes(std::vector<std::shared_ptr<HSVStripNode>> inNodes);

        const std::vector<std::shared_ptr<HSVStripNode>>& getNodes() const;
    private:
        HSVStrip* parentStrip{nullptr};
        std::vector<std::shared_ptr<HSVStripNode>> Nodes;

        int id;

    public:
        HSVStrip* getParentStrip() const { return parentStrip; }
        int getId() const { return id; }

    };
}