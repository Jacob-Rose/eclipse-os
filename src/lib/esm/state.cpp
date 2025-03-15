// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state.h"

#include "../ecore/logging.h"
#include "state_machine.h"

using namespace ecore;
using namespace ecore::log;
using namespace esm;
using namespace std;

State::State() : State("untitled")
{
}

State::State(const char *InStateName)
{
    stateName = InStateName;
}


void State::init()
{
#if DEBUG_LOGGING_ENABLED
    std::string msg = "init state: ";
    msg.append(GetStateName());
    dbgLog(msg.c_str(), Verbosity::Display, Category::State | Category::Library);
#endif
}

void State::cleanup()
{
#if DEBUG_LOGGING_ENABLED
    std::string msg = "cleanup state: ";
    msg.append(GetStateName());
    dbgLog(msg.c_str(), Verbosity::Display, Category::State | Category::Library);
#endif
}

void esm::State::tick(float deltaTime)
{
#if DEBUG_LOGGING_ENABLED
    std::string msg = "ticking leds: ";
    msg.append(GetStateName());
    dbgLog(msg.c_str(), Verbosity::VeryVerbose, Category::OnTick | Category::State | Category::Library);
#endif

    timeStateActive += std::chrono::duration<double>(deltaTime);
    runStateTransitionTest();
}

void State::onStateBegin()
{
#if DEBUG_LOGGING_ENABLED
    std::string msg = "activating state: ";
    msg.append(GetStateName());
    dbgLog(msg.c_str(), Verbosity::Display, Category::State | Category::Library);
#endif

    timeStateActive = std::chrono::duration<double>(0);
}

void State::onStateEnd()
{
#if DEBUG_LOGGING_ENABLED
    std::string msg = "deactivating state: ";
    msg.append(GetStateName());
    dbgLog(msg.c_str(), Verbosity::Display, Category::State | Category::Library);
#endif
}

void State::addStateTransition(weak_ptr<State> inState, TransitionLambda lambda)
{
    stateTransitions[inState] = lambda;
}

weak_ptr<State> State::runStateTransitionTest() const
{
    for (const auto& transition : stateTransitions)
    {
        if (auto targetState = transition.first.lock())
        {
            bool bTransitionConditionMet = transition.second(targetState.get(), const_cast<State*>(this)); // Check if the transition condition is met
            if (bTransitionConditionMet)
            {
                return transition.first; // Return the corresponding state
            }
        }
    }


    return std::weak_ptr<State>(); // Return nullptr if no transition condition is met
}

chrono::duration<double> State::GetStateActiveDuration() const
{
    return timeStateActive;
}