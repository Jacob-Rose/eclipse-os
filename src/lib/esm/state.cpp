// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state.h"

#include "../ecore/logging.h"

using namespace ecore;
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
#if LOGGING_ENABLED
    std::string tickMsg = "init state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::cleanup()
{
#if LOGGING_ENABLED
    std::string tickMsg = "cleanup state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::onStateBegin()
{
#if LOGGING_ENABLED
    std::string tickMsg = "activating state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif

    activationTime = std::chrono::system_clock::now();

    tickStartTime = std::chrono::system_clock::now();
}

void State::onStateEnd()
{
#if LOGGING_ENABLED
    std::string tickMsg = "deactivating state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::tick(float deltaTime)
{
}

void State::runTick()
{
#if LOGGING_ENABLED
    std::string tickMsg = "ticking leds: ";
    tickMsg.append(GetStateName());
    //dbgPrint(tickMsg.c_str(), Verbosity::VeryVerbose, Category::OnTick | Category::StateInfo);
    
    //dbgPrint(std::to_string(GetStateActiveDuration().count()));
#endif

    lastFrameDT = chrono::system_clock::now() - tickStartTime;
    tickStartTime = chrono::system_clock::now();

    tick(lastFrameDT.count());
}

void State::addStateTransition(weak_ptr<State> inState, TransitionLambda lambda)
{
    stateTransitions[inState] = lambda;
}

weak_ptr<State> State::runStateTransitionTest() const
{
    /* TODO  integrate w state machine */
    /*
    for (const auto& transition : stateTransitions)
    {
        bool bTransitionConditionMet = transition.second(this, this); // Check if the transition condition is met
        if (transition.second.operator()) // Check if the transition condition is met
        {
            return transition.first; // Return the corresponding state
        }
    }
        */

    return std::weak_ptr<State>(); // Return nullptr if no transition condition is met
}

chrono::duration<double> State::GetStateActiveDuration() const
{
    chrono::duration<double> timeDiff = tickStartTime - activationTime;
    return timeDiff;
}

chrono::duration<double> State::GetTimeSinceTickStarted() const
{
    chrono::time_point<chrono::system_clock> currentTime = chrono::system_clock::now();
    chrono::duration<double> timeDiff = currentTime - tickStartTime;
    return timeDiff;
}

void StateMachine::setActiveState(shared_ptr<State> InNextState)
{
    // TODO
}
