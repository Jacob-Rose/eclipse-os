// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state.h"

#include "../gm.h"
#include "../lib/ecore/logging.h"

using namespace ecore;

State::State(const char* InStateName)
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
    tickStartTime_Screen = std::chrono::system_clock::now();
}

void State::onStateEnd()
{
#if LOGGING_ENABLED
    std::string tickMsg = "deactivating state: ";
    tickMsg.append(GetStateName());
    dbgLog(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif

    GameManager& GM = GameManager::get();
    GM.screenDrawer.cancelGifRender();
}

void State::tick()
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

    lastFrameDT = std::chrono::system_clock::now() - tickStartTime;
    tickStartTime = std::chrono::system_clock::now();


    tick();
}

void State::addStateTransition(std::weak_ptr<State> inState, TransitionLambda lambda)
{
    stateTransitions[inState] = lambda;
}

std::chrono::duration<double> State::GetStateActiveDuration() const
{
    std::chrono::duration<double> timeDiff = tickStartTime - activationTime;
    return timeDiff;
}

std::chrono::duration<double> State::GetTimeSinceTickStarted() const
{
    std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
    std::chrono::duration<double> timeDiff = currentTime - tickStartTime;
    return timeDiff;
}