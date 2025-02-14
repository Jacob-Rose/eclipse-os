// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "obelisk.h"

#include "lib/ecore/logging.h"

#include "states/state_generic.h"
#include "states/state_obelisk.h"
#include "states/state_boxing.h"

#include <chrono>
#include <ctime>

using namespace ecore;
using namespace eio;

void Obelisk::setup()
{
    GameManager& GM = GameManager::get();

    // Boot state so we can run leds while doing processing for inits (like loading images)
    std::shared_ptr<State> ObeliskState = std::make_shared<State_Boxing_Noise>("mainstate");
    States.push_back(ObeliskState);

    setActiveState(ObeliskState);

    bSetupComplete = true;
}

void Obelisk::setup1()
{
    // nothing needed here
}

void Obelisk::tick()
{
    GameManager& GM = GameManager::get();
    
    lastFrameDT = std::chrono::system_clock::now() - tickStartTime;
    tickStartTime = std::chrono::system_clock::now();

    GM.tick(lastFrameDT.count());

    if(ActiveState != nullptr)
    {
        ActiveState->runTick();
    }

    /*
    for(auto stateTransition : ActiveState->stateTransitions)
    {
        bool bStateTransitionShouldOccur = stateTransition.second(ActiveState.get(), stateTransition.first.lock().get());
        if(bStateTransitionShouldOccur)
        {
            setActiveState(stateTransition.first.lock());
        }
    }
    */

    GM.showLeds();
}

void Obelisk::tickScreen()
{
    GameManager& GM = GameManager::get();

    lastFrameDT_Screen = std::chrono::system_clock::now() - tickStartTime_Screen;
    tickStartTime_Screen = std::chrono::system_clock::now();

#ifdef USE_SCREEN

    GM.screenDrawer.tick(lastFrameDT_Screen.count());

#endif
}

void Obelisk::loop()
{
    tick();
}

void Obelisk::loop1()
{
    if(bSetupComplete)
    {
        tickScreen();
    }
}

void Obelisk::setActiveState(std::shared_ptr<State> InState)
{
    if(ActiveState)
    {
        ActiveState->onStateEnd();
    }

    ActiveState = InState;

    if(ActiveState)
    {
        ActiveState->onStateBegin();
    }
}

