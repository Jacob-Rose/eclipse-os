// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state.h"

#include "../ecore/logging.h"

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
    runStateTickLambdas(deltaTime);
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

void State::addStateTickLambda(int id, TickLambda Lambda)
{
#if ERROR_CHECKING_ENABLED
    if (stateTickLambdas.find(id) != stateTickLambdas.end())
    {
        std::string msg = "State tick lambda with id: " + std::to_string(id) + " already exists!";
        dbgLog(msg.c_str(), Verbosity::Error, Category::State | Category::Library);
        return; // Prevent overwriting existing lambda
    }
#endif    
    stateTickLambdas[id] = Lambda;
}

void State::removeStateTickLambda(int id)
{
#if ERROR_CHECKING_ENABLED
    if (stateTickLambdas.find(id) == stateTickLambdas.end())
    {
        std::string msg = "State tick lambda with id: " + std::to_string(id) + " does not exist!";
        dbgLog(msg.c_str(), Verbosity::Error, Category::State | Category::Library);
        return; // Prevent removing non-existing lambda
    }
#endif
    stateTickLambdas.erase(id);
}

void State::runStateTickLambdas(float deltaTime) const
{
    for (const auto& tickLambda : stateTickLambdas)
    {
        tickLambda.second(deltaTime);
    }
}

chrono::duration<double> State::GetStateActiveDuration() const
{
    return timeStateActive;
}


void StateMachine::setActiveState(shared_ptr<State> InNextState)
{
    NextState = InNextState;
    currentTransitionTime = 0.0f;
}

void StateMachine::init()
{
}

void StateMachine::cleanup()
{
}

void StateMachine::tick(float deltaTime)
{
    //TODO
}
