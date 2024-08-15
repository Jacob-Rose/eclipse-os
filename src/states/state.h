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

namespace ecore
{
    /* @brief States are the structures that really handle all of the associated properties. They handle input themselves.
    * States support generalized lambda based transitions for easily writing inline setters
    */
    class State
    {
    public:

        State(const char* InStateName);
        virtual void init();
        virtual void cleanup();

        virtual void tick();

        //logic level only, no rendering logic here
        virtual void onStateBegin();
        virtual void onStateEnd();

        using TransitionLambda = std::function<bool(State* TargetState, State* MyState)>;

        // lambda passes in the owning state
        void addStateTransition(std::weak_ptr<State> State,  TransitionLambda Lambda);

        const std::string& GetStateName() const { return stateName; }

        std::chrono::duration<double> GetStateActiveDuration() const;     // returned in seconds
        std::chrono::duration<double> GetLastFrameDelta() const;     // returned in seconds

        // TODO this should be private
        std::map<std::weak_ptr<State>, TransitionLambda, std::owner_less<std::weak_ptr<State>>> stateTransitions;

        void runTick();

    protected:
        // used for accurately simulating time between frames
        std::chrono::duration<double> lastFrameDT;

        float framerate = 60.0f; // aim for 60fps

        uint8_t* gifData = nullptr;
        int gifSize = 0;

    private:

        bool bInit = false;
        bool bInitScreen = false;

        //used for tracking ticks in a consistant manner
        std::chrono::time_point<std::chrono::system_clock> tickStartTime;
        std::chrono::time_point<std::chrono::system_clock> tickStartTime_Screen;

        
        // a little inefficient, but very convient, if we could get an fname like system for strings would be nice.
        std::map<std::string, float> animAttributes;

        std::chrono::time_point<std::chrono::system_clock> activationTime;

        std::string stateName;
    };
}