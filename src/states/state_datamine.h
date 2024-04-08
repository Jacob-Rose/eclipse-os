// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "state_base.h"

#include "../jcolors.h"

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

    float theaterSinWidth = 4.0f;

    uint8_t maxHue = 220;
    uint8_t minHue = 170;

    j::HSVPalette palette = std::vector<j::HSV>({
        j::HSV(0.0f,1.0f,1.0f)
    });

private:
    // state info
    float currentScreenAngle = 0;
};