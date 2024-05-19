// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include <AnimatedGIF.h>

#include "../../lib/j/janim.h"

class State_Settings : public State
{
public:
    State_Settings(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    j::LFO gearLFO = j::LFO(6.0f, 4.0f);

private:
    bool bBlueButtonSeenPressed;
};