// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../../lib/eanim/lfo.h"
#include "../../../lib/eanim/noise.h"
#include "../../../lib/eanim/generator_hsv.h"
#include "../../../lib/eanim/processor_float.h"

#include "../../pendant/pendant_generic_state.h"

#include "../../../kits/palettes.h"

using namespace ecore;
using namespace eanim;

class Pattern_SystemOverload : public GeneratorHSV
{
public:

    void init();

    PerlinNoiseGenerator2D staticNoise;
    PerlinNoiseGenerator2D lightningNoise;

    HSV staticColor = HSV(340.0f, 0.76f, 0.8f);
    HSV lightningColor = HSV(60.0f, 1.0f, 0.8f);

    Lerper buttonALerper;
    Lerper buttonBLerper;

    bool bIsButtonAActive = false; 
    bool bIsButtonBActive = false;

    bool bIsButtonAActiveLast = false;
    bool bIsButtonBActiveLast = false;

    bool bStopStrobing = false;

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;

};

class State_SystemOverload : public State_PendantGeneric
{
public:
    State_SystemOverload(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
    virtual void tick(float deltaTime) override;

    virtual void onStateChangeState(StateStatus inStatus) override;
};