// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../pendant/pendant_generic_state.h"

using namespace esm;

class Pattern_Sleep : public GeneratorHSV
{
public:
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override { inOutColor = HSV(0.0f, 0.0f, 0.0f); }
};


class State_Sleep : public State_PendantGeneric
{
public:
    State_Sleep(const char* InStateName,  RelicIO* inIO);

    virtual void init() override;
};