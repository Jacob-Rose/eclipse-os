// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "necklace.h"

#include "logging.h"

#include "states/states.h"

#include <chrono>
#include <ctime>

void Necklace::setup()
{
    std::shared_ptr<State> BootState = std::make_shared<State_Boot>("Boot_State");
    BootState->init();
    States.push_back(BootState);

    std::shared_ptr<State> EmoteState = std::make_shared<State_Emote>("Emote_State");
    EmoteState->init();
    States.push_back(EmoteState);

    std::shared_ptr<State> HeartbeatState = std::make_shared<State_Heartbeat>("Heartbeat_State");
    HeartbeatState->init();
    States.push_back(HeartbeatState);

    std::shared_ptr<State> HackerState = std::make_shared<State_Hacker>("Hacker_State");
    HackerState->init();
    States.push_back(HackerState);

    std::shared_ptr<State> Serendipity = std::make_shared<State_Serendipidy>("Serendipity_State");
    Serendipity->init();
    States.push_back(Serendipity);

    std::shared_ptr<State> SettingsState = std::make_shared<State_Settings>("Settings_State");
    SettingsState->init();
    States.push_back(SettingsState);

    setActiveState(Serendipity);
}

void Necklace::setup1()
{
    // nothing needed here
}

void Necklace::tickLEDs()
{
    if(ActiveState != nullptr)
    {
        ActiveState->runTickLEDs();
    }
}

void Necklace::tickScreen()
{
    if(ActiveState != nullptr)
    {
        ActiveState->runTickScreen();
    }
}

void Necklace::tickLogic()
{
    if(ActiveState != nullptr)
    {
        ActiveState->runTickLogic();
    }
}

void Necklace::loop()
{
    tickLogic();
    tickLEDs();
}

void Necklace::loop1()
{
    tickScreen(); // todo move to loop 1
}

void Necklace::setActiveState(std::shared_ptr<State> InState)
{
    if(ActiveState.get() != nullptr)
    {
        ActiveState->stateDeactivated();
    }

    ActiveState = InState;

    if(ActiveState.get() != nullptr)
    {
        ActiveState->stateActivated();
    }
}