// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jacket.h"

#include "../lib/eio/strip_projection.h"
#include "../lib/ecore/logging.h"

#include "jacket_states.h"
#include "jacket_io.h"

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

    stateMachine = std::make_unique<StateMachine_GenericHSV>();
    stateMachine->setRelicIO(jacketIO);

    std::shared_ptr<Pattern_Jacket_RainbowRoad> mainPattern = std::make_shared<Pattern_Jacket_RainbowRoad>();
    mainPattern->init();

    mainPatternState = std::make_shared<State_GenericHSV>("mainState", jacketIO);
    mainPatternState->setGenerator(mainPattern);
    mainPatternState->init();

    // Start State Machine
    stateManager = std::make_unique<StateManager>();
    int mainPatternStateID = stateManager->addState(mainPatternState);
    stateMachine->setActiveState(mainPatternState);
    stateMachine->init();
}

void jacket::JacketCore::tick(float deltaTime)
{
    if (stateMachine)
    {
        stateMachine->tick(deltaTime);
    }
}

void jacket::JacketCore::tick2()
{
    if (JacketIO* jacketIO = static_cast<JacketIO*>(coreIO.get()))
    {
        jacketIO->tick2();
    }
}
