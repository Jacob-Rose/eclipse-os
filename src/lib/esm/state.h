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
        using ShouldTransitionLambda = std::function<bool(State* TargetState, State* MyState)>;
        using TickLambda = std::function<void(float deltaTime)>;

        // lambda passes in the owning state
        void addStateTransition(std::weak_ptr<State> State,  ShouldTransitionLambda Lambda);

        int addStateTickLambda(TickLambda Lambda);
        void removeStateTickLambda(int id);

        // runs all state transitions and returns first one that returns true
        std::weak_ptr<State> runStateTransitionTest() const;

        int getStateID() const; // set by owning state manager

        const std::string& GetStateName() const { return stateName; }
        StateStatus GetStatus() const { return status; } // returns current state status

        std::chrono::duration<double> GetStateActiveDuration() const;     // returned in seconds

    private:
        std::map<std::weak_ptr<State>, ShouldTransitionLambda, std::owner_less<std::weak_ptr<State>>> stateTransitions;
        std::map<int, TickLambda> stateTickLambdas;
        std::chrono::duration<double> timeStateActive;

        int stateTickLambdaIdIncrementer{1}; // unique id for each tick lambda, incremented for each new lambda added

        std::string stateName;
        bool bInit = false;

    
        StateStatus status;
    protected:
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

        float transitionTime = 20.5f;

        virtual void tick(float deltaTime) override;
    
        void setActiveState(std::shared_ptr<State> inNewState);
        void setNextState(std::shared_ptr<State> inNextState);
        std::shared_ptr<State> getActiveState() const { return ActiveState; } // returns the currently active state
        std::shared_ptr<State> getNextState() const { return NextState; }

        bool isInTransition() const;

    protected:
        std::shared_ptr<State> ActiveState;
        std::shared_ptr<State> NextState;

        float currentTransitionTime;
    private:
        bool bTickedNextStateLast{false};
    };


    class StateManager
    {
    public:
        StateManager() = default;

        int addState(std::shared_ptr<State> state);
        void removeState(int id);

        std::shared_ptr<State> getStateForId(int id) const;

    protected:
        int idIncrement{1}; // unique id for each state, incremented for each new state added
        std::map<int, std::shared_ptr<State>> states; // map of state id to state object
    };
}
