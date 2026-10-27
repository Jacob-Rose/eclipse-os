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

#include <ecore/core.h>
#include <ecore/tickable.h>

#include <eio/hsv_strip.h>

#include "state_blend.h"

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
    /* @brief One state of a StateMachine. Handles its own input, ticks its own
    * logic, and can carry lambda-based transitions and per-tick lambdas.
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

        /// Adds a transition test; the lambda is passed the target and this state.
        void addStateTransition(std::weak_ptr<State> State,  ShouldTransitionLambda Lambda);

        int addStateTickLambda(TickLambda Lambda);
        void removeStateTickLambda(int id);

        /// Runs every transition test and returns the first that passes.
        std::weak_ptr<State> runStateTransitionTest() const;

        int getStateID() const; // set by owning state manager

        const std::string& GetStateName() const { return stateName; }
        StateStatus GetStatus() const { return status; }

        std::chrono::duration<double> GetStateActiveDuration() const;     // seconds

    private:
        std::map<std::weak_ptr<State>, ShouldTransitionLambda, std::owner_less<std::weak_ptr<State>>> stateTransitions;
        std::map<int, TickLambda> stateTickLambdas; // todo make this a array of structs with id and lambda
        std::chrono::duration<double> timeStateActive;

        int stateTickLambdaIdIncrementer{1}; // unique id for each tick lambda, incremented for each new lambda added

        std::string stateName;
        bool bInit = false;

        // Off until a machine enters it; the status is read before first entry
        StateStatus status{StateStatus::Off};
    protected:
        int stateManagerId {0}; // set by state manager

    public:
        friend class StateMachine;
        friend class StateManager;
    };


    /* @brief Runs one active State and cross-fades to the next over
    * transitionTime, using a StateBlend.
    *
    *     machine.setActiveState(idle);      // no fade: the starting state
    *     machine.setNextState(flash);       // fades, from wherever it is
    *     machine.setBlend("wipe");          // how the next fades look
    *     machine.restartState(flash);       // take the current state from the top
    *
    * @see StateBlend for the built-in blends
    */
    class StateMachine : public Tickable
    {
    public:
        StateMachine();

        virtual void init();
        virtual void cleanup();

        float transitionTime = 2.5f;

        virtual void tick(float deltaTime) override;

        void setActiveState(std::shared_ptr<State> inNewState);

        /// Starts a transition to inNextState from wherever the machine is,
        /// mid-fade included: the picture showing is frozen and blended from.
        /// Naming the state already on its way in is a no-op; naming the
        /// active state while settled is refused (use restartState). An
        /// instant blend or a transitionTime of zero lands at once.
        void setNextState(std::shared_ptr<State> inNextState);

        // ---- the blend: what the rig shows between two states ----------

        const StateBlend& getBlend() const { return *blend; }
        void setBlend(const StateBlend& inBlend) { blend = &inBlend; }
        /// By name (`cut`, `crossfade`, `rgb`, `dip`, `wipe`). False and
        /// unchanged for a name that is not a built-in.
        bool setBlend(const char* name);

        /// 0 -> 1 through the running transition; 1 when there is none.
        float getTransitionProgress() const;

        /// The buffer holding the outgoing picture: the active state's, or
        /// kSnapshotBuffer after a retarget mid-fade. kNoBuffer when there is
        /// no outgoing picture (the machine's first fade).
        int getBlendSourceBuffer() const;

        static constexpr int kSnapshotBuffer = -1;
        static constexpr int kNoBuffer = 0;

        /// Re-enters the state already showing or arriving, going through Off
        /// so entry logic runs again. For a cue that means "take it from the top".
        void restartState(std::shared_ptr<State> inState);
        std::shared_ptr<State> getActiveState() const { return ActiveState; }
        std::shared_ptr<State> getNextState() const { return NextState; }

        bool isInTransition() const;

    protected:
        /// Copies what every node shows now into kSnapshotBuffer. The base
        /// machine has no nodes; the one that renders them overrides this.
        virtual void captureBlendSource() {}

        std::shared_ptr<State> ActiveState;
        std::shared_ptr<State> NextState;

        float currentTransitionTime{0.0f};
    private:
        const StateBlend* blend{&defaultStateBlend()};
        bool bBlendFromSnapshot{false};
    };


    /* @brief Owns states and hands out their ids. */
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
