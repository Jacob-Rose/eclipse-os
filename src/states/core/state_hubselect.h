// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/jcolors.h"
#include "../../lib/j/janim.h"

#include <AnimatedGIF.h>

// outfit should pulse a light pastel purple/pink
// lfo on necklace with lfo on speed, moves like an inchworm
// actual hub selection will be done in usage case
class State_HubSelect : public State
{
public:
    State_HubSelect(const char* InStateName);

    virtual void init() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    AnimatedGIF MoonGif;

    j::HSVPalette OutfitPalette;

    j::LFO lfoNecklaceOuter = j::LFO(1600.0f, 4.0f);
    j::LFO lfoInchwormSpeed = j::LFO(400.0f, 10.0f);
    float inchwormSpeed = 400.0f;
};