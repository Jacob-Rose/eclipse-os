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

// Hold Button A -> Speed up turbines
class Pattern_Jacket_WarpTurbines : public GeneratorHSV
{
public:
    Pattern_Jacket_WarpTurbines();

public:
    HSVPalette turbinePaletteA { HSV(35.f, 0.9f, 0.9f), HSV(35.f, 0.05f, 0.5f) };
    HSVPalette turbinePaletteB { HSV(190.f, 0.9f, 0.9f), HSV(190.f, 0.05f, 0.5f)};

    LFO turbineLFO;
    LFO heightLFO;

public:
    virtual void init();

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
};

class State_WarpTurbines : public State_PendantGeneric
{
public:
    State_WarpTurbines(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
};