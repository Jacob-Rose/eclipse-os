// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../pendant/pendant_generic_state.h"

#include "../../../lib/eanim/lfo.h"

class Pattern_Settings : public GeneratorHSV
{
public:

    void init();
    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;

    LFO lfoGear;
};

class State_Settings : public State_PendantGeneric
{
public:
    State_Settings(const char* InStateName, RelicIO* inIO);

    virtual void init() override;
    virtual void tick(float deltaTime) override;

private:

    bool bBlueButtonSeenPressed = false;
};