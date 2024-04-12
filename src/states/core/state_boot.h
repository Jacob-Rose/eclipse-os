// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

// Boot State also handles initializing the other states using the screen thread, 
// so code will look a little weird and hyperspecific
class State_Boot : public State
{
public:
    State_Boot(const char* InStateName);

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    void addStateToInit(std::weak_ptr<State> stateToInit);
    bool hasInitializedAllStates() const { return bInitializedAllStates; }

    uint16_t currentHue = 0;

    float rotationSpeed = 0.5f;

private:
    float currentRotation; // normalized 0-1

    std::vector<std::weak_ptr<State>> statesToInit;
    bool bInitializedAllStates = false;
};