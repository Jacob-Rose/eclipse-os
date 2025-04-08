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

        // returns if the command was handled/consumed
        virtual bool handleCommand(string msg) { return false; }

        void runTick();

    protected:
        unique_ptr<RelicIO> coreIO;
    
    private:
        // used for accurately simulating time between frames
        std::chrono::duration<double> lastFrameDT;
        std::chrono::time_point<std::chrono::system_clock> tickStartTime;
    };
}