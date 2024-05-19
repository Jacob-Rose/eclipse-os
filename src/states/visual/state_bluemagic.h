// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

// a reinterpretation of https://github.com/davepl/DavesGarageLEDSeries/blob/master/ fire effect
#pragma once

#include "../state_base.h"

#include "../../lib/j/janim.h"
#include "../../lib/j/jpalettes.h"

#include <AnimatedGIF.h>

class State_BlueMagic : public State
{
public:
    State_BlueMagic(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    //j::FireEmitter fireEmitter;

    j::HSVPalette firePalette = j::p_bluemagic;

    j::LFO fireOffset = j::LFO(1.0f, 40.0f);

    j::LFO lfoNecklaceOuter = j::LFO(2.0f, 8.0f);
    j::LFO lfoInchwormSpeed = j::LFO(2.0f, 1.0f);
    float inchwormSpeed = 12.0f;
};