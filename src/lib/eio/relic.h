// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>
#include <chrono>
#include <vector>
#include <map>

#include "../ecore/tickable.h"
#include "../eio/screen_drawer.h"
#include "../elink/relic_link.h"
#include "hsv_strip.h"

using namespace ecore;
using namespace std;

namespace eio
{
    enum class EBrightness
    {
        NIGHTTRIP,
        MIN,
        MED,
        HIGH,
        BLINDING,
        MAX,
        COUNT 
    };

    static uint8_t getEBrightnessAsByte(EBrightness inBrightness);

    class RelicIO : public Tickable
    {
    public:

        RelicIO();

        virtual void init();

        virtual void tick(float deltaTime) override;

        virtual ScreenDrawer* getScreenDrawer() const { return nullptr; }
        void showLeds();
        
        EBrightness getGlobalBrightness() const;
        void setGlobalBrightness(EBrightness newBrightness);

    public:
        // each relic can define an enum for the bytes to be per-device specific
        std::map<uint8_t, unique_ptr<HSVStripSegment>> strip_segments;
        std::map<uint8_t, unique_ptr<HSVStrip>> strips;

        EBrightness currentBrightness;
    };

    class RelicCore : public Tickable
    {
    public:

        RelicCore();

        virtual void init() {};

        virtual void preTick();
        virtual void tick(float deltaTime) override;
        virtual void postTick();

        /// Runs in place of tick() while a desk owns the pixels over elink.
        ///
        /// Default is nothing at all, because on most relics tick() *is* the
        /// pattern and computing a look that is about to be overwritten is the
        /// expensive half of a frame. Override it for whatever must keep
        /// running through a takeover - buttons, sensors, a watchdog - and
        /// leave the rendering out.
        virtual void tickWhileLinked(float deltaTime) { (void)deltaTime; }

        // returns if the command was handled/consumed
        virtual bool handleCommand(string msg) { return false; }

        void runTick();

        /// The desk's end of the cable. Give it a transport and it comes alive;
        /// without one it costs a branch per tick and nothing else.
        elink::RelicLink& getLink() { return link; }

        /// The relic's strips and segments. Public because the link writes
        /// through it and a test reads back through it.
        RelicIO* getIO() const { return coreIO.get(); }

        std::chrono::time_point<std::chrono::steady_clock> getTickStartTime() { return tickStartTime; }

    protected:
        unique_ptr<RelicIO> coreIO;
        elink::RelicLink link;

    private:
        // used for accurately simulating time between frames
        std::chrono::duration<double> lastFrameDT;
        std::chrono::time_point<std::chrono::steady_clock> tickStartTime;

        // A default-constructed steady_clock time_point is its epoch, so
        // without this the *first* frame's delta is however long the board has
        // been powered - and every animation on the relic jumps that far in one
        // step before the first pixel is lit.
        bool hasTicked{false};
    };
}