// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/janim.h"
#include "../../lib/j/jpalettes.h"

#include <AnimatedGIF.h>

class State_EnchantedForest : public State
{
public:
    State_EnchantedForest(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    j::HSVPalette palette = j::p_iceCream;

    j::LFO lfo1 = j::LFO(5.0f, 8.0f);
    j::LFO lfo2 = j::LFO(-5.0f, 6.0f);
};