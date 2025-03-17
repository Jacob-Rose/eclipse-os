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
    enum StateStatus
    {
        Off,
        TransitionIn,
        TransitionOut,
        Active
    };
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

    protected:
        virtual void onStateChangeState(StateStatus inStatus);

        void runStateTickLambdas(float deltaTime) const;

    public:
        using ShouldTransitionLambda = function<bool(State* TargetState, State* MyState)>;
        using TickLambda = function<void(float deltaTime)>;

        // lambda passes in the owning state
        void addStateTransition(weak_ptr<State> State,  ShouldTransitionLambda Lambda);

        int addStateTickLambda(TickLambda Lambda);
        void removeStateTickLambda(int id);

        // runs all state transitions and returns first one that returns true
        weak_ptr<State> runStateTransitionTest() const;

        int getStateID() const; // set by owning state manager

        const string& GetStateName() const { return stateName; }

        chrono::duration<double> GetStateActiveDuration() const;     // returned in seconds

    private:
        std::map<weak_ptr<State>, ShouldTransitionLambda, owner_less<weak_ptr<State>>> stateTransitions;
        std::map<int, TickLambda> stateTickLambdas;
        chrono::duration<double> timeStateActive;

        int stateTickLambdaIdIncrementer{1}; // unique id for each tick lambda, incremented for each new lambda added

        std::string stateName;
        bool bInit = false;

        StateStatus status;

        int stateManagerId {0}; // set by state manager

    public:
        friend class StateMachine;
        friend class StateManager;
    };


    class StateMachine : public Tickable
    {
    public:
        StateMachine();

        virtual void init();
        virtual void cleanup();

        virtual void tick(float deltaTime) override;
    
        void setActiveState(shared_ptr<State> inNewState);
        void setNextState(shared_ptr<State> inNextState);

        bool isInTransition() const;

    protected:

        float transitionTime = 8.0f;

    private:
        shared_ptr<State> ActiveState;
        shared_ptr<State> NextState;

        float currentTransitionTime;
    };


    class StateManager
    {
    public:
        StateManager() = default;

        int addState(shared_ptr<State> state);
        void removeState(int id);

        shared_ptr<State> getStateForId(int id) const;

    protected:
        int idIncrement{1}; // unique id for each state, incremented for each new state added
        std::map<int, shared_ptr<State>> states; // map of state id to state object
    };
}
