// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "state_base.h"

class State_DipToBlack : public State
{
public:
    State_DipToBlack(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    bool bScreenToBlack = true;
    float pixelsFadeTime = 0.8f;

    std::weak_ptr<State> stateTransitionOnFinish;
};