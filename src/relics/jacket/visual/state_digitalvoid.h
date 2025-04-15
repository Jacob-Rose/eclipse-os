// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../../lib/eanim/lfo.h"
#include "../../../lib/eanim/noise.h"
#include "../../../lib/eanim/generator_hsv.h"
#include "../../../lib/eanim/processor_float.h"

#include "../../../relics/pendant/pendant_generic_state.h"

using namespace ecore;
using namespace eanim;


// Hold Button A -> Slow down noise change speed
class Pattern_DigitalVoid : public GeneratorHSV
{
public:
    Pattern_DigitalVoid();

public:
    HSV targetColor = HSV(285.f, 0.55f, 0.7f);
    float hueShiftVariance = 55.0f;

    PerlinNoiseGenerator2D coreNoise;
    PerlinNoiseGenerator2D hueShiftNoise;

    Lerper buttonALerper;
    Lerper buttonBLerper;

    bool bIsButtonAActive = false; 
    bool bIsButtonBActive = false;

    bool bIsButtonAActiveLast = false;
    bool bIsButtonBActiveLast = false;

    void init();

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;

};


class State_DigitalVoid : public State_PendantGeneric
{
public:
    State_DigitalVoid(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
    virtual void tick(float deltaTime) override;
};