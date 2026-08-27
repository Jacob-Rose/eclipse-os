// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state.h"

#include "../ecore/logging.h"

using namespace ecore;
using namespace ecore::log;
using namespace esm;

std::string getStateStatusAsString(StateStatus inStatus)
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
    msg.append(GetStateName() + " | ");
    msg.append(getStateStatusAsString(inNewStatus));
    dbgLog(msg.c_str(), Verbosity::Display, Category::State | Category::Library);
#endif

    if(inNewStatus == StateStatus::Active)
    {
        timeStateActive = std::chrono::duration<double>(0);
    }

    status = inNewStatus;
}

void State::addStateTransition(std::weak_ptr<State> inState, ShouldTransitionLambda lambda)
{
    stateTransitions[inState] = lambda;
}

std::weak_ptr<State> State::runStateTransitionTest() const
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
        dbgLog("Running state tick lambda");
        tickLambda.second(deltaTime);
    }
}

std::chrono::duration<double> State::GetStateActiveDuration() const
{
    return timeStateActive;
}

void StateMachine::setActiveState(std::shared_ptr<State> inNewState)
{
#if ERROR_CHECKING_ENABLED
    if(inNewState == nullptr)
    {
        std::string msg = "StateMachine::setActiveState - inNewState is null!";
        dbgLog(msg.c_str(), Verbosity::Error, Category::State | Category::Library);
        return;
    }
    if(inNewState == ActiveState)
    {
        std::string msg = "StateMachine::setActiveState - inNewState is the same as ActiveState!";
        dbgLog(msg.c_str(), Verbosity::Error, Category::State | Category::Library);
        return;
    }
#endif

    if(ActiveState)
    {
        ActiveState->onStateChangeState(StateStatus::Off);
    }
    inNewState->onStateChangeState(StateStatus::Active);
    currentTransitionTime = 0.0f;
    NextState = nullptr;

    ActiveState = inNewState;
}

void StateMachine::setNextState(std::shared_ptr<State> inNextState)
{
#if ERROR_CHECKING_ENABLED
    if(inNextState == nullptr)
    {
        std::string msg = "StateMachine::setNextState - inNextState is null!";
        dbgLog(msg.c_str(), Verbosity::Error, Category::State | Category::Library);
        return;
    }
    if(inNextState == ActiveState)
    {
        std::string msg = "StateMachine::setNextState - inNextState is the same as ActiveState!";
        dbgLog(msg.c_str(), Verbosity::Error, Category::State | Category::Library);
        return;
    }
#endif

    if(ActiveState)
    {
        ActiveState->onStateChangeState(StateStatus::TransitionOut);
    }
    inNextState->onStateChangeState(StateStatus::TransitionIn);
    currentTransitionTime = 0.0f;

    NextState = inNextState;
}

void StateMachine::restartState(std::shared_ptr<State> inState)
{
    // Only a state the machine is actually running can be restarted; anything
    // else is a caller confused about what is showing, and re-entering an Off
    // state would fight whatever is.
    if (inState == nullptr || (inState != ActiveState && inState != NextState))
    {
        return;
    }

    inState->onStateChangeState(StateStatus::Off);
    inState->onStateChangeState(inState == NextState ? StateStatus::TransitionIn
                                                     : StateStatus::Active);
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

    if(NextState)
    {
        currentTransitionTime += deltaTime;
        if(currentTransitionTime > transitionTime)
        {
            setActiveState(NextState);
        }
        else
        {
            NextState->tick(deltaTime);
        }
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

int StateManager::addState(std::shared_ptr<State> inState)
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

std::shared_ptr<State> esm::StateManager::getStateForId(int id) const
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
