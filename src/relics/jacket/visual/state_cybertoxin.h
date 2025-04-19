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

class Pattern_CyberToxin : public GeneratorHSV
{
public:

    void init();

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;

};

class State_CyberToxin : public State_PendantGeneric
{
public:
    State_CyberToxin(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
};