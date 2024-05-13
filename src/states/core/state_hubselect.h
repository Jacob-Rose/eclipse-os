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

    virtual void tick() override;
    virtual void tickScreen() override;

    AnimatedGIF img;

    j::HSVPalette OutfitPalette = {
        j::HSV(0.82f, 80, 190),
        j::HSV(0.77f, 230, 255),
        j::HSV(0.66f, 180, 120)
    };

    j::HSVPalette WhipPalette = {
        j::HSV(0.82f, 80, 20),
        j::HSV(0.77f, 230, 255),
        j::HSV(0.66f, 180, 120)
    };

    j::LFO lfoNecklaceOuter = j::LFO(2.0f, 8.0f);
    j::LFO lfoInchwormSpeed = j::LFO(1.0f, 10.0f);

    j::Saw hueSaw = j::Saw(1.0f);
    float inchwormSpeed = 120.0f;
};