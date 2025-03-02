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

#include "../ecore/core.h"

#include "state.h"

using namespace std;

namespace esm
{
    class StateMachine : public Tickable
    {
    public:
        StateMachine();

        virtual void init();
        virtual void cleanup();

        virtual void tick(float deltaTime) override;

        void addState(shared_ptr<State> NewState);

    protected:
        void setActiveState(shared_ptr<State> NextState);

        float transitionTime = 8.0f;

    private:
        vector<shared_ptr<State>> States;
        shared_ptr<State> ActiveState;
        shared_ptr<State> NextState;

        float currentTransitionTime;
    };
}
