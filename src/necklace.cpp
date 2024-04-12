// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "necklace.h"

#include "gm.h"

#include "lib/j/jio.h"
#include "lib/j/jlogging.h"

#include "states/core/state_sleep.h"
#include "states/visual/state_serendipity.h"
#include "states/visual/state_datamine.h"
#include "states/visual/state_hacker.h"
#include "states/visual/state_drip.h"
#include "states/core/state_boot.h"
#include "states/core/state_settings.h"

#include "states/debug/state_buttontest.h"

#include <chrono>
#include <ctime>

void Necklace::setup()
{
    GlobalManager& GM = GlobalManager::get();

    GM.setGlobalBrightness(128);

    // Boot state so we can run leds while doing processing for inits (like loading images)
    std::shared_ptr<State_Boot> Boot = std::make_shared<State_Boot>("Boot");
    Boot->init();
    States.push_back(Boot);

    std::shared_ptr<State_Hacker> Hacker = std::make_shared<State_Hacker>("Hacker");
    States.push_back(Hacker);

    std::shared_ptr<State_Drip> Drip = std::make_shared<State_Drip>("Drip");
    States.push_back(Drip);

    std::shared_ptr<State_Datamine> Datamine = std::make_shared<State_Datamine>("Datamine");
    States.push_back(Datamine);

    std::shared_ptr<State_Serendipidy> Serendipity = std::make_shared<State_Serendipidy>("Serendipity");
    States.push_back(Serendipity);

    std::shared_ptr<State_Settings> Settings = std::make_shared<State_Settings>("Settings");
    States.push_back(Settings);

    std::shared_ptr<State_ButtonTest> ButtonTest = std::make_shared<State_ButtonTest>("ButtonTest");
    States.push_back(ButtonTest);

    std::shared_ptr<State_Sleep> Sleep = std::make_shared<State_Sleep>("Sleep");
    States.push_back(Sleep);

    Boot->addStateToInit(Hacker);
    Boot->addStateToInit(Drip);
    Boot->addStateToInit(Datamine);
    Boot->addStateToInit(Serendipity);
    Boot->addStateToInit(Settings);
    Boot->addStateToInit(ButtonTest);
    Boot->addStateToInit(Sleep);

    Boot->addStateTransition(Datamine, [](State* current, State* target){
        State_Boot* Boot = static_cast<State_Boot*>(current);
        return Boot->hasInitializedAllStates();
    });

    /*
    Serendipity->addStateTransition(Heartbeat, [](State* current, State* target){
        GlobalManager& MyGM = GlobalManager::get();
        return MyGM.GreenButton->isPressed();
    });

    Heartbeat->addStateTransition(Serendipity, [](State* current, State* target){
        GlobalManager& MyGM = GlobalManager::get();
        return current->GetStateActiveDuration().count() > 4.0f;
    });
    */

    setActiveState(Boot);
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