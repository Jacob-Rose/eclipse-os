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

// The std names the relics use, and no `using namespace std;`: that clashes
// with Arduino's byte and map() once the core builds at gnu++23. std::map is
// qualified below for the same reason.
using std::string;
using std::unique_ptr;
using std::shared_ptr;
using std::make_unique;
using std::make_shared;

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

    uint8_t getEBrightnessAsByte(EBrightness inBrightness);

    /* @brief A relic's hardware: its strips, the segments of nodes a look
    * renders through, and the global brightness. Subclass per relic.
    */
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
        // keyed by a per-relic enum of segment ids
        std::map<uint8_t, unique_ptr<HSVStripSegment>> strip_segments;
        std::map<uint8_t, unique_ptr<HSVStrip>> strips;

        EBrightness currentBrightness;
    };

    /* @brief A relic's program: owns the RelicIO, ticks the looks, answers
    * console commands and the desk's link. Subclass per relic and override
    * tick(); the sketch calls runTick() once a frame.
    */
    class RelicCore : public Tickable
    {
    public:

        RelicCore();

        virtual void init() {};

        virtual void preTick();
        virtual void tick(float deltaTime) override;
        virtual void postTick();

        /// Runs in place of tick() while a desk owns the pixels over elink.
        /// Override for what must keep running through a takeover (buttons,
        /// sensors, a look's clocks) and leave the rendering out.
        virtual void tickWhileLinked(float deltaTime) { (void)deltaTime; }

        /// A console or link command line. Return true when handled.
        virtual bool handleCommand(string msg) { (void)msg; return false; }

        void runTick();

        /// The desk's end of the cable. Give it a transport and it comes alive.
        elink::RelicLink& getLink() { return link; }

        /// The relic's strips and segments.
        RelicIO* getIO() const { return coreIO.get(); }

        std::chrono::time_point<std::chrono::steady_clock> getTickStartTime() { return tickStartTime; }

    protected:
        unique_ptr<RelicIO> coreIO;
        elink::RelicLink link;

    private:
        // used for accurately simulating time between frames
        std::chrono::duration<double> lastFrameDT;
        std::chrono::time_point<std::chrono::steady_clock> tickStartTime;

        // so the first frame's delta is not the time since power-on
        bool hasTicked{false};
    };
}