// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jacket.h"

#include "../../lib/eio/strip_projection.h"
#include "../../lib/ecore/logging.h"

#include "jacket_io.h"

#include "core/state_settings.h"
#include "core/state_sleep.h"

#include "visual/state_warpturbines.h"
#include "visual/state_rainbowroad.h"
#include "visual/state_bluemagic.h"
#include "visual/state_warpturbines.h"
#include "visual/state_campfire.h"
#include "visual/state_hitstop.h"
#include "visual/state_redrum.h"
#include "visual/state_enchantedforest.h"
#include "visual/state_datamine.h"
#include "visual/state_breathewithme.h"
#include "visual/state_digitalvoid.h"

#include "../pendant/pendant_generic_state.h"

using namespace jacket;
using namespace eio;
using namespace ecore;
using namespace ecore::log;

void JacketIO::tick(float deltaTime)
{
    PendantIO::tick(deltaTime);
}

JacketCore::JacketCore()
{
}

void JacketCore::init()
{
    PendantCore::init();
    
    // Initialize the jacket IO and state machine
    coreIO = std::make_unique<JacketIO>();
    JacketIO* jacketIO = static_cast<JacketIO*>(coreIO.get());
    jacketIO->init();
    stateManager = std::make_unique<StateManager>();
    stateMachine = std::make_unique<StateMachine_GenericHSV>();
    stateMachine->transitionTime = 0.75f;
    stateMachine->setRelicIO(jacketIO);

    std::shared_ptr<State_WarpTurbines> warpTurbinesState = std::make_shared<State_WarpTurbines>("warp_turbines", jacketIO);
    warpTurbinesState->init();

    std::shared_ptr<State_Jacket_RainbowRoad> rainbowRoadState = std::make_shared<State_Jacket_RainbowRoad>("rainbowRoad", jacketIO);
    rainbowRoadState->init(); 

    std::shared_ptr<State_Datamine> datamineState = std::make_shared<State_Datamine>("datamine", jacketIO);
    datamineState->init();

    std::shared_ptr<State_Sleep> sleepState = std::make_shared<State_Sleep>("sleep", jacketIO);
    sleepState->init();

    std::shared_ptr<State_Settings> settingsState = std::make_shared<State_Settings>("settings", jacketIO);
    settingsState->init();

    std::shared_ptr<State_BreatheWithMe> breatheWithMeState = std::make_shared<State_BreatheWithMe>("breathe_with_me", jacketIO);
    breatheWithMeState->init();

    std::shared_ptr<State_EnchantedForest> enchantedForestState = std::make_shared<State_EnchantedForest>("enchanted_forest", jacketIO);
    enchantedForestState->init();

    std::shared_ptr<State_BlueMagic> blueMagicState = std::make_shared<State_BlueMagic>("blue_magic", jacketIO);
    blueMagicState->init();

    std::shared_ptr<State_DigitalVoid> digitalVoidState = std::make_shared<State_DigitalVoid>("digital_void", jacketIO);
    digitalVoidState->init();

    std::shared_ptr<State_Hitstop> hitstopState = std::make_shared<State_Hitstop>("hitstop", jacketIO);
    hitstopState->init();

    std::shared_ptr<State_RedRum> redRumState = std::make_shared<State_RedRum>("redrum", jacketIO);
    redRumState->init();

    stateManager->addState(warpTurbinesState);
    stateManager->addState(rainbowRoadState);
    stateManager->addState(datamineState);
    stateManager->addState(sleepState);
    stateManager->addState(settingsState);
    stateManager->addState(breatheWithMeState);
    stateManager->addState(enchantedForestState);
    stateManager->addState(blueMagicState);
    stateManager->addState(digitalVoidState);
    stateManager->addState(hitstopState);
    stateManager->addState(redRumState);
    
    //
    // DIGITAL VOID STATE
    //

    digitalVoidState->addStateTransition(enchantedForestState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getBlueButton();
        return button->runButtonPressedScan();
    });
    
    digitalVoidState->addStateTransition(settingsState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });
    
    digitalVoidState->addStateTransition(datamineState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getRedButton();
        return button->runButtonPressedScan();
    });

    //
    // DATAMINE
    //

    datamineState->addStateTransition(digitalVoidState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });
    
    datamineState->addStateTransition(redRumState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getRedButton();
        return button->runButtonPressedScan();
    });


    //
    // Red Rum
    //

    redRumState->addStateTransition(datamineState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });
    
    redRumState->addStateTransition(hitstopState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getBlueButton();
        return button->runButtonPressedScan();
    });

    
    //
    // Hitstop
    //

    hitstopState->addStateTransition(redRumState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });

    //
    // Sleep
    //

    sleepState->addStateTransition(settingsState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });

    //
    // Settings
    //

    settingsState->addStateTransition(sleepState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getRedButton();
        return button->runButtonPressedScan();
    });

    settingsState->addStateTransition(digitalVoidState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });

    //
    // Enchanted Forest
    //

    enchantedForestState->addStateTransition(digitalVoidState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });

    enchantedForestState->addStateTransition(warpTurbinesState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getBlueButton();
        return button->runButtonPressedScan();
    });

    enchantedForestState->addStateTransition(rainbowRoadState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getRedButton();
        return button->runButtonPressedScan();
    });

    //
    // Warp Turbine
    //

    warpTurbinesState->addStateTransition(enchantedForestState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });

    warpTurbinesState->addStateTransition(breatheWithMeState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getBlueButton();
        return button->runButtonPressedScan();
    });

    //
    // Breathe with me
    //

    breatheWithMeState->addStateTransition(warpTurbinesState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });

    //
    // Rainbow Road
    //

    rainbowRoadState->addStateTransition(enchantedForestState, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });


    stateMachine->setNextState(digitalVoidState);
    stateMachine->init();
}

void jacket::JacketCore::tick(float deltaTime)
{
    if (stateMachine)
    {
        stateMachine->tick(deltaTime);
    }
}
