// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "necklace.h"

#include "gm.h"

#include "lib/j/jio.h"
#include "lib/j/jlogging.h"

#include "states/core/state_boot.h"
#include "states/core/state_settings.h"
#include "states/core/state_hubselect.h"
#include "states/core/state_sleep.h"

#include "states/visual/state_serendipity.h"
#include "states/visual/state_datamine.h"
#include "states/visual/state_emote_heart.h"

#include "states/visual/state_drip.h"
#include "states/visual/state_enchantedforest.h"
#include "states/visual/state_ritual.h"
#include "states/visual/state_onering.h"
#include "states/visual/state_bluemagic.h"
#include "states/visual/state_spellcaster.h"

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

    std::shared_ptr<State_Drip> Drip = std::make_shared<State_Drip>("Drip");
    States.push_back(Drip);
    Boot->addStateToInit(Drip);

    std::shared_ptr<State_Datamine> Datamine = std::make_shared<State_Datamine>("Datamine");
    States.push_back(Datamine);
    Boot->addStateToInit(Datamine);

    std::shared_ptr<State_Serendipidy> Serendipity = std::make_shared<State_Serendipidy>("Serendipity");
    States.push_back(Serendipity);
    Boot->addStateToInit(Serendipity);

    std::shared_ptr<State_Settings> Settings = std::make_shared<State_Settings>("Settings");
    States.push_back(Settings);
    Boot->addStateToInit(Settings);

    std::shared_ptr<State_ButtonTest> ButtonTest = std::make_shared<State_ButtonTest>("ButtonTest");
    States.push_back(ButtonTest);
    Boot->addStateToInit(ButtonTest);

    std::shared_ptr<State_Sleep> Sleep = std::make_shared<State_Sleep>("Sleep");
    States.push_back(Sleep);
    Boot->addStateToInit(Sleep);

    std::shared_ptr<State_HubSelect> EclipseHub = std::make_shared<State_HubSelect>("EclipseHub");
    States.push_back(EclipseHub);
    Boot->addStateToInit(EclipseHub);

    std::shared_ptr<State_Emote_Heart> Emote_Heart = std::make_shared<State_Emote_Heart>("Emote_Heart");
    States.push_back(Emote_Heart);
    Boot->addStateToInit(Emote_Heart);

    std::shared_ptr<State_Spellcaster> Spellcaster = std::make_shared<State_Spellcaster>("Spellcaster");
    States.push_back(Spellcaster);
    Boot->addStateToInit(Spellcaster);
    
    std::shared_ptr<State_Ritual> Ritual = std::make_shared<State_Ritual>("Ritual");
    States.push_back(Ritual);
    Boot->addStateToInit(Ritual);

    std::shared_ptr<State_OneRing> OneRing = std::make_shared<State_OneRing>("OneRing");
    States.push_back(OneRing);
    Boot->addStateToInit(OneRing);

    std::shared_ptr<State_BlueMagic> BlueMagic = std::make_shared<State_BlueMagic>("BlueMagic");
    States.push_back(BlueMagic);
    Boot->addStateToInit(BlueMagic);

    std::shared_ptr<State_EnchantedForest> EnchantedForest = std::make_shared<State_EnchantedForest>("EnchantedForest");
    States.push_back(EnchantedForest);
    Boot->addStateToInit(EnchantedForest);

    /* BOOT TRANSITION */

    Boot->addStateTransition(EclipseHub, [](State* current, State* target){
        State_Boot* BootState = static_cast<State_Boot*>(current);
        // cause i set this up incorrectly, we need to wait for this to be 0
        return BootState->sawFillPercentage.evaluate(0.0f) < 0.02f; 
    });

    // EclipseHub < - > Settings

    EclipseHub->addStateTransition(Settings, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Settings->addStateTransition(EclipseHub, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    // Settings < - > Sleep

    Settings->addStateTransition(Sleep, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.RedButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Sleep->addStateTransition(Settings, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    // EclipseHub < - > Spellcaster

    EclipseHub->addStateTransition(Spellcaster, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.GreenButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });
    
    Spellcaster->addStateTransition(EclipseHub, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    // EclipseHub < - > Drip

    EclipseHub->addStateTransition(Drip, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.BlueButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Drip->addStateTransition(EclipseHub, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });
    

    // Spellcaster < - > BlueMagic

    Spellcaster->addStateTransition(BlueMagic, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.GreenButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    BlueMagic->addStateTransition(Spellcaster, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    // Spellcaster < - > Ritual

    Spellcaster->addStateTransition(Ritual, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.RedButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Ritual->addStateTransition(Spellcaster, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    // Ritual < - > OneRing

    Ritual->addStateTransition(OneRing, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.RedButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    OneRing->addStateTransition(Ritual, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    // EclipseHub < - > Drip

    EclipseHub->addStateTransition(Drip, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.RedButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Drip->addStateTransition(EclipseHub, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });


    // Drip < - > EnchantedForest

    Drip->addStateTransition(EnchantedForest, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.BlueButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    EnchantedForest->addStateTransition(Drip, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    // Drip < - > Serendipity

    Drip->addStateTransition(Serendipity, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.GreenButton.get();
        return Necklace::runButtonHeldTestAndReset(Button);
    });

    Serendipity->addStateTransition(Drip, [](State* current, State* target){
        GameManager& MyGM = GameManager::get();
        j::Button* Button = MyGM.WhiteButton.get();
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

void Necklace::tick()
{
    GameManager& GM = GameManager::get();
    
    lastFrameDT = std::chrono::system_clock::now() - tickStartTime;
    tickStartTime = std::chrono::system_clock::now();

    GM.tick(lastFrameDT.count());

    if(ActiveState != nullptr)
    {
        ActiveState->runTick();
    }

    for(auto stateTransition : ActiveState->stateTransitions)
    {
        bool bStateTransitionShouldOccur = stateTransition.second(ActiveState.get(), stateTransition.first.lock().get());
        if(bStateTransitionShouldOccur)
        {
            setActiveState(stateTransition.first.lock());
        }
    }

    GM.showLeds();
}

void Necklace::tickScreen()
{
    GameManager& GM = GameManager::get();

    lastFrameDT_Screen = std::chrono::system_clock::now() - tickStartTime_Screen;
    tickStartTime_Screen = std::chrono::system_clock::now();

    GM.ScreenDrawer.tick(lastFrameDT_Screen.count());
}

void Necklace::loop()
{
    tick();
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