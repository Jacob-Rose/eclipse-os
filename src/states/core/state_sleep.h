// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include <AnimatedGIF.h>

class State_Sleep : public State
{
public:
    State_Sleep(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;
};