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
        std::map<int, TickLambda> stateTickLambdas; // todo make this a array of structs with id and lambda
        std::chrono::duration<double> timeStateActive;

        int stateTickLambdaIdIncrementer{1}; // unique id for each tick lambda, incremented for each new lambda added

        std::string stateName;
        bool bInit = false;

        // Off until a machine enters it: a state's status is read before its
        // first entry now (StateMachine::tick skips an Off active), so it
        // cannot be left to whatever the allocator had there.
        StateStatus status{StateStatus::Off};
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

        float transitionTime = 2.5f;

        virtual void tick(float deltaTime) override;
    
        void setActiveState(std::shared_ptr<State> inNewState);

        /* @brief Starts a transition to inNextState, from wherever the machine is.
        *
        * Not only from a settled state: asked for a third look while a
        * cross-fade is still running, the machine freezes the picture it is
        * showing and blends from *that* to the new target, so any state can
        * be cued at any moment and the rig never snaps back to the look it
        * was already leaving. The dropped state is told Off. Naming the state
        * already on its way in is a no-op; naming the active one while
        * settled is refused, as before - restartState is the way to re-enter.
        *
        * A blend that isInstant(), or a transitionTime of zero, lands the
        * state at once rather than a frame later.
        */
        void setNextState(std::shared_ptr<State> inNextState);

        // ---- the blend: what the rig shows between two states ----------
        //
        // Held as a pointer to a built-in, never owned: blends are stateless
        // and one instance serves every machine. See state_blend.h.

        const StateBlend& getBlend() const { return *blend; }
        void setBlend(const StateBlend& inBlend) { blend = &inBlend; }
        /// By the name a cue line spells. False and unchanged for a name
        /// that is not a built-in.
        bool setBlend(const char* name);

        /// 0 -> 1 through the running transition; 1 when there is none.
        float getTransitionProgress() const;

        /// Where the outgoing picture lives, for whoever mixes the nodes: the
        /// active state's own buffer, or - after a retarget mid-fade - the
        /// frozen picture in kSnapshotBuffer. kNoBuffer when there is no
        /// outgoing picture at all (the machine's very first fade).
        int getBlendSourceBuffer() const;

        static constexpr int kSnapshotBuffer = -1;
        static constexpr int kNoBuffer = 0;

        /* @brief Re-enters a state the machine is already showing or blending toward.
        *
        * setNextState refuses a self-transition, and should - but a cue that
        * names the current state means "take it from the top". The scanner
        * leans on this: a machine starts in its first state at launch, so a
        * boot's first cue can name a state that is already active with its
        * clock long run out. The re-entry goes through Off so it reads as a
        * genuine entry to states whose entry logic keys on the edge.
        */
        void restartState(std::shared_ptr<State> inState);
        std::shared_ptr<State> getActiveState() const { return ActiveState; } // returns the currently active state
        std::shared_ptr<State> getNextState() const { return NextState; }

        bool isInTransition() const;

    protected:
        /// Copies what every node is showing right now into kSnapshotBuffer.
        /// The base machine has no nodes; the one that renders them does.
        virtual void captureBlendSource() {}

        std::shared_ptr<State> ActiveState;
        std::shared_ptr<State> NextState;

        float currentTransitionTime{0.0f};
    private:
        const StateBlend* blend{&defaultStateBlend()};
        bool bBlendFromSnapshot{false};
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
