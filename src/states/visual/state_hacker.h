// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

class State_Hacker : public State
{
public:
    State_Hacker(const char* InStateName);

    virtual void tick() override;
    virtual void tickScreen() override;

    // config 
    float screenRotateSpeed = 1.0f;

private:
    // state info
    float currentScreenAngle = 0;
};