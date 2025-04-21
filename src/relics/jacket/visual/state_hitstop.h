// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../../lib/eanim/lfo.h"
#include "../../../lib/eanim/generator_hsv.h"
#include "../../../lib/eanim/processor_float.h"

#include "../../pendant/pendant_generic_state.h"

#include "../../../kits/palettes.h"

using namespace ecore;
using namespace eanim;

inline HSVPalette p_hitstop {
    HSV(2.0f, 0.88f, 1.0f),
    HSV(30.0f, 0.6f, 1.0f),
};

class Pattern_Hitstop : public GeneratorHSV
{
public:

    void init();

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;

    void activateHitstopA();

    HSVPalette mainPalette = p_hitstop;
    HSV hitColor = HSV(0.0f, 0.0f, 1.0f);

private:
    LFO brightnessLFO;
    LFO colorLFO;
    Lerper hitstopALerper;

    float timeSinceHitActivate = 9999.0f;

};

class State_Hitstop : public State_PendantGeneric
{
public:
    State_Hitstop(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
    virtual void tick(float deltaTime) override;
};