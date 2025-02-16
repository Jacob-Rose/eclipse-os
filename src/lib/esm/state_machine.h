// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <map>
#include <functional>
#include <memory>
#include <string>
#include <chrono>
#include <ctime>
#include <map>

#include "state.h"

using namespace std;

namespace eas
{
    class StateMachine
    {
    public:
        void addState();
        void setActiveState(shared_ptr<State> NewState);

        float transitionTime = 8.0f;

    private:
        vector<shared_ptr<State>> States;
        shared_ptr<State> ActiveState;
        shared_ptr<State> NextState;

        float currentTransitionTime;
    };
}
