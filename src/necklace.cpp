// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "necklace.h"

#include "gm.h"
#include "jio.h"

#include "jlogging.h"

#include "states/states.h"

#include <chrono>
#include <ctime>

void Necklace::setup()
{
    GlobalManager& GM = GlobalManager::get();

    std::shared_ptr<State_Boot> BootState = std::make_shared<State_Boot>("Boot_State");
    BootState->init();
    States.push_back(BootState);

    std::shared_ptr<State_Emote> EmoteState = std::make_shared<State_Emote>("Emote_State");
    States.push_back(EmoteState);

    std::shared_ptr<State_Heartbeat> HeartbeatState = std::make_shared<State_Heartbeat>("Heartbeat_State");
    States.push_back(HeartbeatState);

    std::shared_ptr<State_Hacker> HackerState = std::make_shared<State_Hacker>("Hacker_State");
    States.push_back(HackerState);

    std::shared_ptr<State_Serendipidy> Serendipity = std::make_shared<State_Serendipidy>("Serendipity_State");
    States.push_back(Serendipity);

    std::shared_ptr<State_Settings> SettingsState = std::make_shared<State_Settings>("Settings_State");
    States.push_back(SettingsState);

    BootState->addStateToInit(EmoteState);
    BootState->addStateToInit(HeartbeatState);
    BootState->addStateToInit(HackerState);
    BootState->addStateToInit(Serendipity);
    BootState->addStateToInit(SettingsState);

    BootState->addStateTransition(Serendipity, [](State* current, State* target){
        State_Boot* Boot = static_cast<State_Boot*>(current);
        return Boot->hasInitializedAllStates();
    });

    Serendipity->addStateTransition(HeartbeatState, [](State* current, State* target){
        GlobalManager& MyGM = GlobalManager::get();
        return MyGM.GreenButton->isPressed();
    });

    HeartbeatState->addStateTransition(Serendipity, [](State* current, State* target){
        GlobalManager& MyGM = GlobalManager::get();
        return current->GetStateActiveDuration().count() > 4.0f;
    });

    setActiveState(BootState);
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

    GlobalManager& GM = GlobalManager::get();
    GM.RingLEDs->show();
    GM.BoardLED->show();

    if(GM.bHasOutfitConnected)
    {
        GM.OutfitLEDs->show();
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

        for(auto stateTransition : ActiveState->stateTransitions)
        {
            bool bStateTransitionShouldOccur = stateTransition.second(ActiveState.get(), stateTransition.first.lock().get());
            if(bStateTransitionShouldOccur)
            {
                setActiveState(stateTransition.first.lock());
            }
        }
    }
}

void Necklace::loop()
{
    tickLogic();
    tickLEDs();
}

void Necklace::loop1()
{
    tickScreen();
}

void Necklace::setActiveState(std::shared_ptr<State> InState)
{
    if(ActiveState.get() != nullptr)
    {
        ActiveState->onStateEnd();
    }

    ActiveState = InState;

    if(ActiveState.get() != nullptr)
    {
        ActiveState->onStateBegin();
    }
}