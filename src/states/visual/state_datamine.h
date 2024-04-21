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

    virtual void init() override;

    virtual void onStateBegin() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    j::LFO lfoNecklaceInner = j::LFO(300.0f, 2.0f);
    j::LFO lfoNecklaceOuter = j::LFO(1600.0f, 8.0f);
    j::LFO lfoArm = j::LFO(-300.0f, 8.5f);
    j::LFO lfoWhip = j::LFO(1500.0f, 16.0f);

    AnimatedGIF gif;

    float idleSpeed = 250.0f;
    float uploadSpeed = 1800.0f;
    float downloadSpeed = -1800.0f;
    j::MomentumFloat activationSpeedRamp = j::MomentumFloat(5000000.0f, 1000000.0f);

    j::HSVPalette idlePalette = {
        j::HSV(0.6f, 190, 170),
        j::HSV(0.8f, 255, 220)
    };

    j::HSVPalette uploadPalette = {
        j::HSV(0.0f, 190, 255),
        j::HSV(0.1f, 190, 255)
    };

    j::HSVPalette downloadPalette = {
        j::HSV(0.3f, 190, 255),
        j::HSV(0.4f, 100, 255)
    };

    private:
        float idlePaletteBuffer = 400.0f;
};