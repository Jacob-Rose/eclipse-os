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

string getStateStatusAsString(StateStatus inStatus)
{
    switch(inStatus)
    {
        case Off: return "Off";
        case TransitionIn: return "TransitionIn";
        case TransitionOut: return "TransitionOut";
        case Active: return "Active";
    }
    return "";
}

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

void State::onStateChangeState(StateStatus inNewStatus)
{
#if DEBUG_LOGGING_ENABLED
    std::string msg = "state status changed: ";
    msg.append(GetStateName());
    msg.append(getStateStatusAsString(inNewStatus));
    dbgLog(msg.c_str(), Verbosity::Display, Category::State | Category::Library);
#endif

    if(inNewStatus == StateStatus::Active)
    {
        timeStateActive = std::chrono::duration<double>(0);
    }
}

void State::addStateTransition(weak_ptr<State> inState, ShouldTransitionLambda lambda)
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

int State::getStateID() const
{
    return stateManagerId;
}

int State::addStateTickLambda(TickLambda Lambda)
{
    ++stateTickLambdaIdIncrementer; // Increment the id for the new lambda
    stateTickLambdas[stateTickLambdaIdIncrementer] = Lambda;
    return stateTickLambdaIdIncrementer;
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

void StateMachine::setActiveState(shared_ptr<State> inNewState)
{
    if(ActiveState)
    {
        ActiveState->onStateChangeState(StateStatus::Off);
    }
    inNewState->onStateChangeState(StateStatus::Active);
    currentTransitionTime = 0.0f;
    NextState = nullptr;

    ActiveState = inNewState;
}

void StateMachine::setNextState(shared_ptr<State> inNextState)
{
    if(ActiveState)
    {
        ActiveState->onStateChangeState(StateStatus::TransitionOut);
    }
    inNextState->onStateChangeState(StateStatus::TransitionIn);
    currentTransitionTime = 0.0f;

    NextState = inNextState;
}

bool StateMachine::isInTransition() const
{
    return NextState.get() != nullptr;
}

StateMachine::StateMachine()
{
}

void StateMachine::init()
{
}

void StateMachine::cleanup()
{
}

void StateMachine::tick(float deltaTime)
{
    if(NextState != nullptr)
    {
        currentTransitionTime += deltaTime;
        if(currentTransitionTime > transitionTime)
        {
            setActiveState(NextState);
        }
        //TODO

        //NextState->tick(deltaTime);
    }
    else
    {
        std::weak_ptr<State> nextState = ActiveState->runStateTransitionTest();
        if(!nextState.expired())
        {
            setNextState(nextState.lock());
        }
    }

    if(ActiveState != nullptr)
    {
        ActiveState->tick(deltaTime);
    }
}

int StateManager::addState(shared_ptr<State> inState)
{
#if ERROR_CHECKING_ENABLED
    if(inState->stateManagerId != 0)
    {
        return 0;
    }
#endif
    idIncrement++;
    states[idIncrement] = inState; // Add the state to the map with a unique id
    inState->stateManagerId = idIncrement;
    return idIncrement;
}

shared_ptr<State> esm::StateManager::getStateForId(int id) const
{ 
    auto it = states.find(id); 
    return (it != states.end()) ? it->second : nullptr;
}

void StateManager::removeState(int id)
{
    auto it = states.find(id);
    if (it != states.end())
    {
        states.erase(it);
    }
}
