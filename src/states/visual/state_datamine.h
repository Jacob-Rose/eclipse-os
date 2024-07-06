// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/janim.h"
#include "../../lib/j/jcolors.h"
#include "../../lib/ext/easing.h"

#include <AnimatedGIF.h>

// A relatively simple theater effect using a sine wave on the LED idx (+ an offset) to make data move.

// possibly a second sine wave that moves faster with a white pulse to visualize data transfer
class State_Datamine : public State
{
public:
    State_Datamine(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    j::LFO lfoNecklaceInner = j::LFO(300.0f, 2.0f);
    j::LFO lfoNecklaceOuter = j::LFO(1600.0f, 8.0f);
    j::LFO lfoArm = j::LFO(-300.0f, 8.5f);
    j::LFO lfoWhip = j::LFO(1500.0f, 16.0f);

    float idleSpeed = 20.0f;
    float uploadSpeed = 180.0f;
    float downloadSpeed = -180.0f;
    j::Sweep activationSpeedRamp = j::Sweep(50000.0f, 10000.0f);

    j::HSVPalette idlePalette = {
        j::HSV(180.f, 1.f, 1.f),
        j::HSV(270.f, 1.f, 1.f)
    };

    j::HSVPalette uploadPalette = {
        j::HSV(0.f, 1.f, 1.f),
        j::HSV(45.f, 1.f, 1.f)
    };

    j::HSVPalette downloadPalette = {
        j::HSV(0.3f, 1.f, 1.f),
        j::HSV(0.4f, 0.5f, 1.f)
    };

    private:
        float idlePaletteBuffer = 400.0f;
};