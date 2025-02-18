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

using namespace std;

namespace esm
{
    /* @brief States are the structures that really handle all of the associated properties. They handle input themselves.
    * States support generalized lambda based transitions for easily writing inline setters
    */
    class State
    {
    public:
        State();
        State(const char* InStateName);

        virtual void init();
        virtual void cleanup();

        void runTick();

    protected:
        virtual void tick(float deltaTime);

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
        chrono::duration<double> GetTimeSinceTickStarted() const;     // returned in seconds

    private:
        std::map<weak_ptr<State>, TransitionLambda, owner_less<weak_ptr<State>>> stateTransitions;

    protected:
        // used for accurately simulating time between frames
        chrono::duration<double> lastFrameDT;

        bool bInit = false;
        bool bInitScreen = false;

        // used for tracking ticks in a consistant manner
        chrono::time_point<chrono::system_clock> tickStartTime;
        
        // maps to a custom enum set up by the specific state
        //std::map<byte, float> animAttributes;

        std::chrono::time_point<std::chrono::system_clock> activationTime;

        std::string stateName;
    };

    class StateMachine
    {
    public:
        void setActiveState(shared_ptr<State> InNextState);

        float transitionTime = 8.0f;

    private:
        shared_ptr<State> ActiveState;
        shared_ptr<State> NextState;

        float currentTransitionTime;
    };
}
