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

    j::HSVPalette palette = j::p_purplesky;

    j::LFO lfoNecklaceOuter = j::LFO(24.0f, 16.0f);
};