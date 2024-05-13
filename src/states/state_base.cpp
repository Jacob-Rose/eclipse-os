// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_base.h"

#include "../lib/j/jlogging.h"
#include "../gm.h" // TODO make more generic

State::State(const char* InStateName)
{
    stateName = InStateName;
}


void State::init()
{
#if LOGGING_ENABLED
    std::string tickMsg = "init state: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::cleanup()
{
#if LOGGING_ENABLED
    std::string tickMsg = "cleanup state: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::onStateBegin()
{
#if LOGGING_ENABLED
    std::string tickMsg = "activating state: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
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
    jlog::print(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif

    GameManager& GM = GameManager::get();
    GM.ScreenDrawer.cancelGifRender();
}

void State::tick()
{
}

void State::tickScreen()
{
}

void State::runTick()
{
#if LOGGING_ENABLED
    std::string tickMsg = "ticking leds: ";
    tickMsg.append(GetStateName());
    //jlog::print(tickMsg.c_str(), Verbosity::VeryVerbose, Category::OnTick | Category::StateInfo);
#endif

    lastFrameDT = std::chrono::system_clock::now() - tickStartTime;
    tickStartTime = std::chrono::system_clock::now();

    jlog::print(std::to_string(GetStateActiveDuration().count()));

    tick();
}

void State::runTickScreen()
{
#if LOGGING_ENABLED
    std::string tickMsg = "ticking screen: ";
    tickMsg.append(GetStateName());
    //jlog::print(tickMsg.c_str(), Verbosity::VeryVerbose, Category::OnTick | Category::StateInfo);
#endif

    lastFrameDT_Screen = std::chrono::system_clock::now() - tickStartTime_Screen;
    tickStartTime_Screen = std::chrono::system_clock::now();

    tickScreen();
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

std::chrono::duration<double> State::GetLastFrameDelta() const
{
    std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
    std::chrono::duration<double> timeDiff = currentTime - tickStartTime;
    return timeDiff;
}