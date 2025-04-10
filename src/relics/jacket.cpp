// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jacket.h"

#include "../lib/eio/strip_projection.h"
#include "../lib/ecore/logging.h"

#include "../imgs/squid.h"
#include "../imgs/kirby.h"

#include "jacket_patterns.h"
#include "jacket_io.h"

#include "pendant_generic_state.h"

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
    
    coreIO = std::make_unique<JacketIO>();
    JacketIO* jacketIO = static_cast<JacketIO*>(coreIO.get());
    jacketIO->init();
    stateManager = std::make_unique<StateManager>();
    stateMachine = std::make_unique<StateMachine_GenericHSV>();
    stateMachine->setRelicIO(jacketIO);

    std::shared_ptr<Pattern_Jacket_WarpTurbines> mainPattern1 = std::make_shared<Pattern_Jacket_WarpTurbines>();
    mainPattern1->init();

    std::shared_ptr<State_PendantGeneric> mainPatternState1 = std::make_shared<State_PendantGeneric>("mainState", jacketIO);
    mainPatternState1->setGenerator(mainPattern1);
    mainPatternState1->setGifData((uint8_t *)squid, sizeof(squid));
    mainPatternState1->init();

    std::shared_ptr<Pattern_Jacket_TheaterLFO> mainPattern2 = std::make_shared<Pattern_Jacket_TheaterLFO>();
    //mainPattern2->init();

    std::shared_ptr<State_PendantGeneric> mainPatternState2 = std::make_shared<State_PendantGeneric>("mainState2", jacketIO);
    mainPatternState2->setGenerator(mainPattern2);
    mainPatternState2->setGifData((uint8_t *)kirby, sizeof(kirby));
    mainPatternState2->init();

    std::shared_ptr<Pattern_Jacket_WarpTurbines> mainPattern3 = std::make_shared<Pattern_Jacket_WarpTurbines>();
    mainPattern3->init();

    std::shared_ptr<State_PendantGeneric> mainPatternState3 = std::make_shared<State_PendantGeneric>("mainState3", jacketIO);
    mainPatternState3->setGenerator(mainPattern3);
    mainPatternState3->init();

    int mainPattern1StateID = stateManager->addState(mainPatternState1);
    int mainPattern2StateID = stateManager->addState(mainPatternState2);
    int mainPattern3StateID = stateManager->addState(mainPatternState3);

    mainPatternState1->addStateTransition(mainPatternState2, [jacketIO](State* current, State* target){
        Button* button = jacketIO->getWhiteButton();
        return button->runButtonPressedScan();
    });


    stateMachine->setActiveState(mainPatternState1);
    stateMachine->init();
}

void jacket::JacketCore::tick(float deltaTime)
{
    if (stateMachine)
    {
        stateMachine->tick(deltaTime);
    }
}
