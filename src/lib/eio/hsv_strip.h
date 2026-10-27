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
#else
// The host has no pixel driver, but relics/wiring.h names a colour order per
// sculpture and is compiled on both sides. Adafruit's values.
typedef uint16_t neoPixelType;
#define NEO_RGB    ((0 << 6) | (0 << 4) | (1 << 2) | (2))
#define NEO_GRB    ((1 << 6) | (1 << 4) | (0 << 2) | (2))
#define NEO_BGR    ((2 << 6) | (2 << 4) | (1 << 2) | (0))
#define NEO_KHZ800 0x0000
#endif

#include "../ecore/hsv.h"

using namespace ecore;

namespace eio
{
    /* @brief Where a strip is plugged in and what it is. Every relic's strips
    * are declared in relics/wiring.h, the only file a pin number lives in.
    */
    struct StripWiring
    {
        uint16_t pin;
        uint16_t length;
        neoPixelType order; // NEO_GRB + NEO_KHZ800 and friends. Ignored on a host.
    };

    /* @brief An LED strip addressed in HSV, over an Adafruit_NeoPixel on the
    * board or a plain buffer on a host. Set colours with setHSV(), then show().
    */
    class HSVStrip
    {
    public:
#if USING_NEOPIXEL
        HSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType);
#endif
        HSVStrip(uint16_t inLedCount, uint16_t inLedPin);
        explicit HSVStrip(const StripWiring& wiring);
        ~HSVStrip();

        HSV getHSV(uint16_t idx) const;
        void setHSV(uint16_t idx, const HSV& hsv);
        void setHSV(uint16_t idx, float h, uint8_t s, uint8_t v);

        /// Writes a pixel straight through, no HSV step and no gamma, for a
        /// frame a desk has already rendered over elink (gamma is applied
        /// exactly once, by the desk). strip_HSV is left alone so the relic's
        /// own look is still there after a handback. The strip brightness
        /// still applies: it is a current limit.
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
        // on a host strip_HSV is the framebuffer; a consumer reads it back
        // through getStripHSV() on show()
        uint8_t hostBrightness{255};

        // what setPixelRGB wrote, since it bypasses strip_HSV. Sized on first use.
        std::vector<uint8_t> hostPixels;
#endif

    public:
#if !USING_NEOPIXEL
        /// What setPixelRGB has written, 3 bytes per pixel. Host builds only.
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

    /* @brief One pixel of a segment, as a look sees it: a place to write a
    * colour, plus per-state buffers the machine blends during a transition.
    * Subclasses add coordinates; GetStripNodeType() is the cast guard, since
    * there is no RTTI.
    */
    class HSVStripNode
    {
    public:
        HSVStripNode(class HSVStripSegment* inParentStrip, int inStripIdx);
        virtual ~HSVStripNode() = default;

        void setHSV(const HSV& hsv);
        void setHSV(float h, uint8_t s, uint8_t v);

        void setBuffer(int idx, const HSV& inHSV);
        HSV getBuffer(int idx) const;
        void clearBuffer(int idx);
        /// every buffer, as (id, colour) pairs
        std::vector<std::pair<int, HSV>> getBufferMap() const;

        virtual StripNodeType GetStripNodeType() const { return StripNodeType::ROOT; }

    private:
        class HSVStripSegment* parentStripSegment;
        int stripIdx{0}; // led index on the strip

        std::map<int, HSV> bufferMap;

    public:
        int getStripIdx() const { return stripIdx; }
        class HSVStripSegment* getStripSegment() const { return parentStripSegment; }
    };


    /* @brief A run of nodes on one strip. */
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