// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

class State_Sleep : public State
{
public:
    State_Sleep(const char* InStateName);

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;
};