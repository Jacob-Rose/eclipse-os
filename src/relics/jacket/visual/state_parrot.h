// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../../lib/eanim/lfo.h"
#include "../../../lib/eanim/generator_hsv.h"

#include "../../pendant/pendant_generic_state.h"

#include "../../../kits/palettes.h"

using namespace ecore;
using namespace eanim;

class Pattern_Parrot : public GeneratorHSV
{
public:

    void init();

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;

    HSVPalette palette = jpalettes::p_parrot;

    LFO offset;

    LFO lfoNecklaceOuter;
    LFO lfoInchwormSpeed;
    float inchwormSpeed;
};

class State_Parrot : public State_PendantGeneric
{
public:
    State_Parrot(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
};