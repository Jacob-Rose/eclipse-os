// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "obelisk.h"

#include <string>

#include "../lib/eio/strip_projection.h"

#include "../states/state_obelisk.h"
#include "../lib/ecore/logging.h"

using namespace obelisk;
using namespace ecore;
using namespace ecore::log;
using namespace eio;
using namespace std;

ObeliskIO::ObeliskIO() : RelicIO()
{
}

void ObeliskIO::init()
{
    RelicIO::init();


    auto [mainStripIt, stripInserted] = strips.emplace(static_cast<uint8_t>(ObeliskStripID::STRIP_MAIN), make_unique<HSVStrip>(WALL_SIDE_LENGTH * 8, 22));


    HSVStrip* mainStrip = mainStripIt->second.get();
    auto [it1, inserted1] = strip_segments.emplace(static_cast<uint8_t>(StripSegmentID::SideA_Up), make_unique<HSVStripSegment>(mainStrip));
    if (inserted1) it1->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, 0                , WALL_SIDE_LENGTH, Coord(0,0), Coord(0, 1.f)));

    auto [it2, inserted2] = strip_segments.emplace(static_cast<uint8_t>(StripSegmentID::SideA_Down), make_unique<HSVStripSegment>(mainStrip));
    if (inserted2) it2->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, WALL_SIDE_LENGTH , WALL_SIDE_LENGTH, Coord(1, WALL_SIDE_LENGTH), Coord(0, -1.f)));
    
    auto [it3, inserted3] = strip_segments.emplace(static_cast<uint8_t>(StripSegmentID::SideB_Up), make_unique<HSVStripSegment>(mainStrip));
    if (inserted3) it3->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, WALL_SIDE_LENGTH * 2, WALL_SIDE_LENGTH, Coord(2,0), Coord(0, 1.f)));
    auto [it4, inserted4] = strip_segments.emplace(static_cast<uint8_t>(StripSegmentID::SideB_Down), make_unique<HSVStripSegment>(mainStrip));
    if (inserted4) it4->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, WALL_SIDE_LENGTH * 3, WALL_SIDE_LENGTH, Coord(3, WALL_SIDE_LENGTH), Coord(0, -1.f)));
    
    auto [it5, inserted5] = strip_segments.emplace(static_cast<uint8_t>(StripSegmentID::SideC_Up), make_unique<HSVStripSegment>(mainStrip));
    if (inserted5) it5->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, WALL_SIDE_LENGTH * 4, WALL_SIDE_LENGTH, Coord(4,0), Coord(0, 1.f)));
    auto [it6, inserted6] = strip_segments.emplace(static_cast<uint8_t>(StripSegmentID::SideC_Down), make_unique<HSVStripSegment>(mainStrip));
    if (inserted6) it6->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, WALL_SIDE_LENGTH * 5, WALL_SIDE_LENGTH, Coord(5, WALL_SIDE_LENGTH), Coord(0, -1.f)));
    
    auto [it7, inserted7] = strip_segments.emplace(static_cast<uint8_t>(StripSegmentID::SideD_Up), make_unique<HSVStripSegment>(mainStrip));
    if (inserted7) it7->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, WALL_SIDE_LENGTH * 6, WALL_SIDE_LENGTH, Coord(6,0), Coord(0, 1.f)));
    auto [it8, inserted8] = strip_segments.emplace(static_cast<uint8_t>(StripSegmentID::SideD_Down), make_unique<HSVStripSegment>(mainStrip));
    if (inserted8) it8->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, WALL_SIDE_LENGTH * 7, WALL_SIDE_LENGTH, Coord(7, WALL_SIDE_LENGTH), Coord(0, -1.f)));
    
    dbgLog("ObeliskIO::init", Verbosity::Verbose, Category::Relic);

    setGlobalBrightness(EBrightness::HIGH);
}

ObeliskCore::ObeliskCore() : RelicCore()
{
    coreIO = make_unique<ObeliskIO>();
    coreIO->init();

    stateMachine = make_unique<StateMachine_GenericHSV>();
    stateMachine->setRelicIO(coreIO.get());
    stateManager = make_unique<StateManager>();

    mainPatternState = make_shared<State_GenericHSV>("mainState", coreIO.get());
    mainPatternState->setGenerator(make_shared<Pattern_Obelisk_FourSeasons>());
    mainPatternState->init();

    theaterPatternState = make_shared<State_GenericHSV>("theaterState", coreIO.get());
    theaterPatternState->setGenerator(make_shared<Pattern_Obelisk_Theater>());
    theaterPatternState->init();

    mainPatternId = stateManager->addState(mainPatternState);
    theaterPatternId = stateManager->addState(theaterPatternState);

    // Start State Machine
    stateMachine->setActiveState(theaterPatternState);
    stateMachine->init();
}


void obelisk::ObeliskCore::tick(float deltaTime)
{
    RelicCore::tick(deltaTime);

    stateMachine->tick(deltaTime); 
}

void obelisk::ObeliskCore::handleCommand(string msg)
{
    if (strcmp(msg.c_str(), "switch") == 0)  // strcmp returns 0 if strings are equal
    {
        dbgLog("ObeliskCore::handleCommand - switching patterns", Verbosity::Display, Category::Relic);
        stateMachine->setNextState(mainPatternState);
    }
}
