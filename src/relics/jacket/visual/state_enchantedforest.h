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


class Pattern_EnchantedForest : public GeneratorHSV
{
public:

    void init();

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;

    HSVPalette palette = jpalettes::p_iceCream;
    Lerper buttonALerper;
    Lerper buttonBLerper;

    bool bIsButtonAActive = false; 
    bool bIsButtonBActive = false;

    bool bIsButtonAActiveLast = false;
    bool bIsButtonBActiveLast = false;

    LFO lfo1;
    LFO lfo2;
};

class State_EnchantedForest : public State_PendantGeneric
{
public:
    State_EnchantedForest(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
    virtual void tick(float deltaTime) override;
};