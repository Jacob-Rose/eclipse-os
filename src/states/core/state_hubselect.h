// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/jcolors.h"
#include "../../lib/j/janim.h"

#include "../../lib/j/jpalettes.h"

#include <AnimatedGIF.h>

// outfit should pulse a light pastel purple/pink
// lfo on necklace with lfo on speed, moves like an inchworm
// actual hub selection will be done in usage case
class State_HubSelect : public State
{
public:
    State_HubSelect(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    j::HSVPalette OutfitPalette = j::p_disney100;

    j::HSVPalette WhipPalette = {
        j::HSV(0.82f, 0.3f, 0.1f),
        j::HSV(0.77f, 0.85f, 1.0f),
        j::HSV(0.66f, 0.6f, 0.5f)
    };

    j::LFO lfoNecklaceOuter = j::LFO(2.0f, 8.0f);
    j::LFO lfoInchwormSpeed = j::LFO(1.0f, 10.0f);

    j::Saw hueSaw = j::Saw(1.0f);
    float inchwormSpeed = 120.0f;
};