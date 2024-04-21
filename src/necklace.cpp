// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "necklace.h"

#include "gm.h"

#include "lib/j/jio.h"
#include "lib/j/jlogging.h"

#include "states/core/state_hubselect.h"
#include "states/core/state_sleep.h"
#include "states/visual/state_serendipity.h"
#include "states/visual/state_datamine.h"
#include "states/visual/state_hacker.h"
//#include "states/visual/state_drip.h"
#include "states/core/state_boot.h"
#include "states/core/state_settings.h"
#include "states/visual/state_emote_heart.h"

#include "states/debug/state_buttontest.h"

#include <chrono>
#include <ctime>

void Necklace::setup()
{
    GameManager& GM = GameManager::get();

    // Boot state so we can run leds while doing processing for inits (like loading images)
    std::shared_ptr<State_Boot> Boot = std::make_shared<State_Boot>("Boot");
    Boot->init();
    States.push_back(Boot);

    //std::shared_ptr<State_Drip> Drip = std::make_shared<State_Drip>("Drip");
    //States.push_back(Drip);

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

    std::shared_ptr<State_HubSelect> Hub = std::make_shared<State_HubSelect>("Hub");
    States.push_back(Hub);

    std::shared_ptr<State_Emote_Heart> Emote_Heart = std::make_shared<State_Emote_Heart>("Emote_Heart");
    States.push_back(Emote_Heart);

    //Boot->addStateToInit(Drip);
    Boot->addStateToInit(Datamine);
    Boot->addStateToInit(Serendipity);
    Boot->addStateToInit(Settings);
    Boot->addStateToInit(Sleep);
    Boot->addStateToInit(Hub);
    Boot->addStateToInit(Emote_Heart);

    Boot->addStateTransition(Hub, [](State* current, State* target){
        State_Boot* Boot = static_cast<State_Boot*>(current);
        return Boot->hasInitializedAllStates();
    });

    Datamine->addStateTransition(Hub, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Hub->addStateTransition(Datamine, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.GreenButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Hub->addStateTransition(Serendipity, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.RedButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Serendipity->addStateTransition(Hub, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Serendipity->addStateTransition(Emote_Heart, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.RedButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Emote_Heart->addStateTransition(Serendipity, [](State* current, State* target){
        return current->GetStateActiveDuration().count() > 4.0f;
    });


    Hub->addStateTransition(Settings, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Settings->addStateTransition(Hub, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Settings->addStateTransition(Sleep, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.RedButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Sleep->addStateTransition(Settings, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.RedButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });


    setActiveState(Boot);

    bSetupComplete = true;
}

void Necklace::setup1()
{
    // nothing needed here
}

bool Necklace::runButtonHeldTestAndReset(j::Button* inButton)
{
    constexpr float HoldThreshold = 0.005f;
    if(inButton->isPressed())
    {
        bool bPressedLongEnough = inButton->getTimeSinceStateChange() > HoldThreshold;
        if(bPressedLongEnough && inButton->bHasBeenReleased)
        {
            inButton->resetTimeSinceStateChange();
            inButton->bHasBeenReleased = false;
            return true;
        }
    }
    else
    {
        inButton->bHasBeenReleased = true;
    }
    return false;
}

void Necklace::tickLEDs()
{
    if(ActiveState != nullptr)
    {
        ActiveState->runTickLEDs();
    }

    GameManager& GM = GameManager::get();
    GM.showLeds();
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

        GameManager& GM = GameManager::get();
        GM.tickLogic(ActiveState->GetLastFrameDelta().count());
    }
}

void Necklace::loop()
{
    tickLogic();
    tickLEDs();
}

void Necklace::loop1()
{
    if(bSetupComplete)
    {
        tickScreen();
    }
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