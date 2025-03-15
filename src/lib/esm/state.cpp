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
    std::string tickMsg = "init state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::cleanup()
{
#if DEBUG_LOGGING_ENABLED
    std::string tickMsg = "cleanup state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void esm::State::tick(float deltaTime)
{
#if DEBUG_LOGGING_ENABLED
    std::string tickMsg = "ticking leds: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::VeryVerbose, Category::OnTick/* | Category::StateInfo */);
#endif

    timeStateActive += std::chrono::duration<double>(deltaTime);
    runStateTransitionTest();
}

void State::onStateBegin()
{
#if DEBUG_LOGGING_ENABLED
    std::string tickMsg = "activating state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif

    timeStateActive = std::chrono::duration<double>(0);
}

void State::onStateEnd()
{
#if DEBUG_LOGGING_ENABLED
    std::string tickMsg = "deactivating state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
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