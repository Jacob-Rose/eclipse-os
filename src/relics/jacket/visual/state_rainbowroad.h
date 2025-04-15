// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#pragma once

#include "../../../lib/eanim/lfo.h"
#include "../../../lib/eanim/generator_hsv.h"
#include "../../../lib/eanim/processor_float.h"

#include "../../pendant/pendant_generic_state.h"

using namespace ecore;
using namespace eanim;

class Pattern_Jacket_RainbowRoad : public GeneratorHSV
{
public:
    Pattern_Jacket_RainbowRoad();

public:
    HSVPalette rainbowPalette { HSV(0.f, 0.55f, 0.7f), HSV(270.f, 0.55f, 0.7f) };

    LFO cogLFO;
    LFO hueLFO;
    LFO cogSpeedLFO;

    Lerper buttonALerper;
    Lerper buttonBLerper;

    bool bIsButtonAActive = false; 
    bool bIsButtonBActive = false;

    bool bIsButtonAActiveLast = false;
    bool bIsButtonBActiveLast = false;

    virtual void init();

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;

};

class State_Jacket_RainbowRoad : public State_PendantGeneric
{
public:
    State_Jacket_RainbowRoad(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
    virtual void tick(float deltaTime) override;
};