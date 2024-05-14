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

class State_Ritual : public State
{
public:
    State_Ritual(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

  protected:
    int     Size;
    int     Cooling = 80;
    int     Sparks = 3;
    int     SparkHeight = 4;
    int     Sparking = 50;
    bool    bReversed = true;
    bool    bMirrored = false;

    byte  * heat;

    // When diffusing the fire upwards, these control how much to blend in from the cells below (ie: downward neighbors)
    // You can tune these coefficients to control how quickly and smoothly the fire spreads.  

    static const byte BlendSelf = 2;
    static const byte BlendNeighbor1 = 3;
    static const byte BlendNeighbor2 = 2;
    static const byte BlendNeighbor3 = 1;
    static const byte BlendTotal = (BlendSelf + BlendNeighbor1 + BlendNeighbor2 + BlendNeighbor3);

    j::HSVPalette firePalette = j::p_ritual;
};