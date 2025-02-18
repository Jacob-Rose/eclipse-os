// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>
#include <chrono>
#include <vector>
#include <map>

#include "../lib/eio/hsv_strip.h"

#include "../lib/esm/state.h"

using namespace ecore;
using namespace std;

namespace eio
{
    enum EBrightness
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

    class RelicIO
    {
    public:

        RelicIO();

        void init();
        void cleanup();

        void tick(float deltaTime);
        void showLeds();
        
        EBrightness getGlobalBrightness() const;
        void setGlobalBrightness(EBrightness newBrightness);

    private:
        // each relic can define an enum for the bytes to be per-device specific
        std::map<uint8_t, shared_ptr<HSVStripSegment>> strip_segments;
        std::map<uint8_t, shared_ptr<HSVStrip>> strips;

        EBrightness currentBrightness;
    };

    class RelicCore
    {
    public:

        RelicCore();

        virtual void init();
        virtual void preTick();
        virtual void tick(float deltaTime) {}

        void runTick() { preTick(); }

    private:
        // used for accurately simulating time between frames
        std::chrono::duration<double> lastFrameDT;
        std::chrono::time_point<std::chrono::system_clock> tickStartTime;

    private:

        bool bSetupComplete = false;
    };
}