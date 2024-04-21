// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/janim.h"

#include <AnimatedGIF.h>

// Boot State also handles initializing the other states using the screen thread, 
// so code will look a little weird and hyperspecific
class State_Boot : public State
{
public:
    State_Boot(const char* InStateName);

    virtual void init() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    // state handling
    void addStateToInit(std::weak_ptr<State> stateToInit);
    bool hasInitializedAllStates() const { return bInitializedAllStates; }

    bool isStateComplete() const;

    uint16_t currentHue = 0;

    AnimatedGIF gif;

    j::Saw sawFillPercentage = j::Saw();

private:
    std::vector<std::weak_ptr<State>> statesToInit;
    bool bInitializedAllStates = false;
};