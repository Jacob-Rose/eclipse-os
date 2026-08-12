// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <vector>
#include <memory>
#include <map>

#include "../ecore/core.h"

// parenthesised so `#if !USING_NEOPIXEL` means what it reads like
#define USING_NEOPIXEL (USE_ARDUINO && true)

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

        /// Writes a pixel straight through, with no HSV step and no gamma.
        ///
        /// This is for a frame that arrived already rendered — a desk driving
        /// the strip over elink. Gamma has to be applied exactly once and the
        /// desk has already done it, so going through setHSV here would correct
        /// a corrected frame and crush everything below mid-brightness toward
        /// black. Quiet failure, not a loud one, which is why it gets its own
        /// door rather than a flag on the existing one.
        ///
        /// strip_HSV is deliberately left alone: it is what the relic's own
        /// patterns read and write, and it should still hold the look that was
        /// running when the desk took over. The next tick after a handback
        /// paints every node anyway.
        ///
        /// The strip's own brightness still applies — it is a current limit,
        /// not a preference, and 344 pixels at full white is 20A.
        void setPixelRGB(uint16_t idx, uint8_t r, uint8_t g, uint8_t b);

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
#else
        // the host build has no pixel driver behind the strip: strip_HSV *is*
        // the framebuffer, and a consumer (see edmx::Renderer) reads it back out
        // via getStripHSV() on show().
        uint8_t hostBrightness{255};

        // ...except for setPixelRGB, which bypasses strip_HSV by design. On a
        // host it lands here instead, so the elink path can be driven and read
        // back with no hardware attached. Sized on first use.
        std::vector<uint8_t> hostPixels;
#endif

    public:
#if !USING_NEOPIXEL
        /// What setPixelRGB has written, 3 bytes per pixel. Host builds only —
        /// this is how a test sees what a relic would have lit.
        const std::vector<uint8_t>& getHostPixels() const { return hostPixels; }
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