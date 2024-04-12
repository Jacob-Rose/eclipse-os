// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/jcolors.h"

class State_ButtonTest : public State
{
public:
    State_ButtonTest(const char* InStateName);

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;
    
    virtual void onStateBegin() override;

    j::HSV OnColor = j::HSV(0,100,100);
    j::HSV OffColor = j::HSV(0,0,0);

    bool bButtonPressed = false;
};