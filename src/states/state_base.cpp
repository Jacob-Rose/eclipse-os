// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_base.h"

#include "../jlogging.h"

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

    tickStartTime_LED = std::chrono::system_clock::now();
    tickStartTime_Screen = std::chrono::system_clock::now();
    tickStartTime_Logic = std::chrono::system_clock::now();

    ledFrame = 0;
    logicFrame = 0;
    screenFrame = 0;
}

void State::onStateEnd()
{
#if LOGGING_ENABLED
    std::string tickMsg = "deactivating state: ";
    tickMsg.append(GetStateName());
    jlog::print(tickMsg.c_str(), Verbosity::Display, Category::StateInfo);
#endif
}

void State::tickLEDs()
{
}

void State::tickScreen()
{
}

void State::tickLogic()
{
}


void State::runTickLEDs()
{
#if LOGGING_ENABLED
    std::string tickMsg = "ticking leds: ";
    tickMsg.append(GetStateName());
    //jlog::print(tickMsg.c_str(), Verbosity::VeryVerbose, Category::OnTick | Category::StateInfo);
#endif

    lastFrameDT_LED = std::chrono::system_clock::now() - tickStartTime_LED;
    tickStartTime_LED = std::chrono::system_clock::now();

    tickLEDs();

    ledFrame++;
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

    screenFrame++;
}

void State::runTickLogic()
{
#if LOGGING_ENABLED
    std::string tickMsg = "ticking logic: ";
    tickMsg.append(GetStateName());
    //jlog::print(tickMsg.c_str(), Verbosity::Verbose, Category::OnTick | Category::StateInfo);
#endif

    lastFrameDT_Logic = std::chrono::system_clock::now() - tickStartTime_Logic;
    tickStartTime_Logic = std::chrono::system_clock::now();

    tickLogic();

    logicFrame++;
}

void State::addStateTransition(std::weak_ptr<State> inState, TransitionLambda lambda)
{
    stateTransitions[inState] = lambda;
}

std::chrono::duration<double> State::GetStateActiveDuration() const
{
    std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
    std::chrono::duration<double> timeDiff = currentTime - activationTime;
    return timeDiff;
}

// currently assumes for logic, but really they are all the same with some offset differences
std::chrono::duration<double> State::GetLastFrameDelta() const
{
    std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
    std::chrono::duration<double> timeDiff = currentTime - tickStartTime_Logic;
    return timeDiff;
}