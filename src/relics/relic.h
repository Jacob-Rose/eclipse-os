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

#include "../states/state.h"

using namespace ecore;
using namespace std;

namespace eio
{
    enum class EBrightness : uint8
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
        map<byte, shared_ptr<HSVStripSegment>> strip_segments;

        EBrightness currentBrightness;
    };

    class RelicCore
    {
    public:
        virtual void init();
        virtual void preTick();
        virtual void tick(float deltaTime);

    protected:
        // used for accurately simulating time between frames
        std::chrono::duration<double> lastFrameDT;
        std::chrono::time_point<std::chrono::system_clock> tickStartTime;

    private:

        bool bSetupComplete = false;
    };
}