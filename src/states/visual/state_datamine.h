// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/janim.h"
#include "../../lib/j/jcolors.h"
#include "../../lib/ext/easing.h"

// A relatively simple theater effect using a sine wave on the LED idx (+ an offset) to make data move.

// possibly a second sine wave that moves faster with a white pulse to visualize data transfer
class State_Datamine : public State
{
public:
    State_Datamine(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    j::LFO lfoNecklaceInner = j::LFO(100.0f, 12.0f);
    j::LFO lfoNecklaceOuter = j::LFO(100.0f, 16.0f);
    j::LFO lfoArm = j::LFO(300.0f, 8.5f);
    j::LFO lfoWhip = j::LFO(1500.0f, 16.0f);

    j::HSVPalette armPalette = std::vector<j::HSV>({
        j::HSV(20000,255,255)
    });

    j::HSVPalette whipPalette = std::vector<j::HSV>({
        j::HSV(20000,255,255)
    });

protected:

private:
    // state info
    float currentLFO1Offset = 0;
    float currentLFO2Offset = 0;
    float currentLFO3Offset = 0;
};