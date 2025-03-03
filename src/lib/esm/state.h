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

#include "../ecore/core.h"
#include "../ecore/tickable.h"

#include "../eio/hsv_strip.h"

using namespace std;
using namespace ecore;
using namespace eio;

namespace esm
{
    /* @brief States are the structures that really handle all of the associated properties. They handle input themselves.
    * States support generalized lambda based transitions for easily writing inline setters
    */
    class State : public Tickable
    {
    public:
        State();
        State(const char* InStateName);

        virtual void init();
        virtual void cleanup();

        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripSegment* segment, HSVStripNode* Node) {}

    protected:
        //logic level only, no rendering logic here
        virtual void onStateBegin();
        virtual void onStateEnd();

        using TransitionLambda = function<bool(State* TargetState, State* MyState)>;

        // lambda passes in the owning state
        void addStateTransition(weak_ptr<State> State,  TransitionLambda Lambda);
        // runs all state transitions and returns first one that returns true
        weak_ptr<State> runStateTransitionTest() const;

        const string& GetStateName() const { return stateName; }

        chrono::duration<double> GetStateActiveDuration() const;     // returned in seconds

    private:
        std::map<weak_ptr<State>, TransitionLambda, owner_less<weak_ptr<State>>> stateTransitions;
        chrono::duration<double> timeStateActive;

        std::string stateName;

        bool bInit = false;
    };
}
