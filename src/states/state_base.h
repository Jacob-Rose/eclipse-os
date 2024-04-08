// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <map>
#include <functional>
#include <memory>
#include <string>
#include <chrono>
#include <ctime>


class State
{
public:

    State(const char* InStateName);
    virtual void init();
    virtual void cleanup();

    // all deltaTimes are relative to their own hardware
    virtual void tickScreen();
    virtual void tickLEDs();
    // screen is rendered on seperate thread, thus we have two delta times
    virtual void tickLogic();

    void runTickScreen();
    void runTickLEDs();
    void runTickLogic();

    //logic level only, no rendering logic here
    virtual void onStateBegin();
    virtual void onStateEnd();

    using TransitionLambda = std::function<bool(State* TargetState, State* MyState)>;

    // lambda passes in the owning state
    void addStateTransition(std::weak_ptr<State> State,  TransitionLambda Lambda);

    const std::string& GetStateName() const { return stateName; }


    std::chrono::duration<double> GetStateActiveDuration() const;     // returned in seconds
    std::chrono::duration<double> GetLastFrameDelta() const;     // returned in seconds

protected:
    // used for accurately simulating time between frames
    std::chrono::duration<double> lastFrameDT_LED;
    std::chrono::duration<double> lastFrameDT_Screen;
    std::chrono::duration<double> lastFrameDT_Logic;

    float framerate = 60.0f; // aim for 60fps

private:

    bool bInit = false;
    bool bInitScreen = false;

    //used for tracking ticks in a consistant manner
    std::chrono::time_point<std::chrono::system_clock> tickStartTime_LED;
    std::chrono::time_point<std::chrono::system_clock> tickStartTime_Screen;
    std::chrono::time_point<std::chrono::system_clock> tickStartTime_Logic;

    uint64_t ledFrame;
    uint64_t logicFrame;
    uint64_t screenFrame;

    std::map<std::weak_ptr<State>, TransitionLambda, std::owner_less<std::weak_ptr<State>>> stateTransitions;
    // a little inefficient, but very convient, if we could get an fname like system for strings would be nice.
    std::map<std::string, float> animAttributes;

    std::chrono::time_point<std::chrono::system_clock> activationTime;

    std::string stateName;

    friend class Necklace;
};