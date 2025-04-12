// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "obelisk.h"

#include <string>

#include "../../lib/eio/strip_projection.h"
#include "../../lib/ecore/logging.h"

#include "state_obelisk.h"

using namespace obelisk;
using namespace ecore;
using namespace ecore::log;
using namespace eio;

ObeliskIO::ObeliskIO() : RelicIO()
{
}

void ObeliskIO::init()
{
    RelicIO::init();

    auto [mainStripIt, stripInserted] = strips.emplace(static_cast<uint8_t>(0), make_unique<HSVStrip>(WALL_SIDE_LENGTH * 8, 22));

    HSVStrip* mainStrip = mainStripIt->second.get();

    // Side A

    int id1 = static_cast<uint8_t>(StripSegmentID::SideA_Up);
    auto [it1, inserted1] = strip_segments.emplace(id1, make_unique<HSVStripSegment>(mainStrip, id1));
    if (inserted1) HSVStripNodeFactory::GenerateAxisRow(it1->second.get(), 0                , WALL_SIDE_LENGTH, Coord(0,0), Coord(0, 1.f));

    int id2 = static_cast<uint8_t>(StripSegmentID::SideA_Down);
    auto [it2, inserted2] = strip_segments.emplace(id2, make_unique<HSVStripSegment>(mainStrip, id2));
    if (inserted2) HSVStripNodeFactory::GenerateAxisRow(it2->second.get(), WALL_SIDE_LENGTH , WALL_SIDE_LENGTH, Coord(1, WALL_SIDE_LENGTH), Coord(0, -1.f));
    
    // Side B

    int id3 = static_cast<uint8_t>(StripSegmentID::SideB_Up);
    auto [it3, inserted3] = strip_segments.emplace(id3, make_unique<HSVStripSegment>(mainStrip, id3));
    if (inserted3) HSVStripNodeFactory::GenerateAxisRow(it3->second.get(), WALL_SIDE_LENGTH * 2, WALL_SIDE_LENGTH, Coord(2,0), Coord(0, 1.f));

    int id4 = static_cast<uint8_t>(StripSegmentID::SideB_Down);
    auto [it4, inserted4] = strip_segments.emplace(id4, make_unique<HSVStripSegment>(mainStrip, id4));
    if (inserted4) HSVStripNodeFactory::GenerateAxisRow(it4->second.get(), WALL_SIDE_LENGTH * 3, WALL_SIDE_LENGTH, Coord(3, WALL_SIDE_LENGTH), Coord(0, -1.f));
    
    // Side C

    int id5 = static_cast<uint8_t>(StripSegmentID::SideC_Up);
    auto [it5, inserted5] = strip_segments.emplace(id5, make_unique<HSVStripSegment>(mainStrip, id5));
    if (inserted5) HSVStripNodeFactory::GenerateAxisRow(it5->second.get(), WALL_SIDE_LENGTH * 4, WALL_SIDE_LENGTH, Coord(4,0), Coord(0, 1.f));

    int id6 = static_cast<uint8_t>(StripSegmentID::SideC_Down);
    auto [it6, inserted6] = strip_segments.emplace(id6, make_unique<HSVStripSegment>(mainStrip, id6));
    if (inserted6) HSVStripNodeFactory::GenerateAxisRow(it6->second.get(), WALL_SIDE_LENGTH * 5, WALL_SIDE_LENGTH, Coord(5, WALL_SIDE_LENGTH), Coord(0, -1.f));
    
    // Side D

    int id7 = static_cast<uint8_t>(StripSegmentID::SideD_Up);
    auto [it7, inserted7] = strip_segments.emplace(id7, make_unique<HSVStripSegment>(mainStrip, id7));
    if (inserted7) HSVStripNodeFactory::GenerateAxisRow(it7->second.get(), WALL_SIDE_LENGTH * 6, WALL_SIDE_LENGTH, Coord(6,0), Coord(0, 1.f));

    int id8 = static_cast<uint8_t>(StripSegmentID::SideD_Down);
    auto [it8, inserted8] = strip_segments.emplace(id8, make_unique<HSVStripSegment>(mainStrip, id8));
    if (inserted8) HSVStripNodeFactory::GenerateAxisRow(it8->second.get(), WALL_SIDE_LENGTH * 7, WALL_SIDE_LENGTH, Coord(7, WALL_SIDE_LENGTH), Coord(0, -1.f));
    
    dbgLog("ObeliskIO::init", Verbosity::Verbose, Category::Relic);

    setGlobalBrightness(EBrightness::HIGH);
}

ObeliskCore::ObeliskCore() : RelicCore()
{
    coreIO = std::make_unique<ObeliskIO>();
    coreIO->init();

    stateMachine = std::make_unique<StateMachine_GenericHSV>();
    stateMachine->setRelicIO(coreIO.get());
    stateManager = std::make_unique<StateManager>();

    mainPatternState = std::make_shared<State_GenericHSV>("mainState", coreIO.get());
    mainPatternState->setGenerator(make_shared<Pattern_Obelisk_FourSeasons>());
    mainPatternState->init();

    theaterPatternState = std::make_shared<State_GenericHSV>("theaterState", coreIO.get());
    theaterPatternState->setGenerator(make_shared<Pattern_Obelisk_Theater>());
    theaterPatternState->init();

    testPatternState = std::make_shared<State_GenericHSV>("testState", coreIO.get());
    testPatternState->setGenerator(std::make_shared<Pattern_Obelisk_Monocolor>());
    testPatternState->init();

    mainPatternId = stateManager->addState(mainPatternState);
    theaterPatternId = stateManager->addState(theaterPatternState);
    testPatternId = stateManager->addState(testPatternState);

    // Start State Machine
    stateMachine->setActiveState(testPatternState);
    stateMachine->init();

    stateChangeTimer.onTimerEvent.add([this]() {
        dbgLog("ObeliskCore::stateChangeTimer - switching patterns", Verbosity::Display, Category::Relic);
        // Switch to the next state after the timer expires
        if (stateMachine->getActiveState() == mainPatternState) {
            stateMachine->setNextState(theaterPatternState);
        } else if (stateMachine->getActiveState() == theaterPatternState) {
            stateMachine->setNextState(testPatternState);
        } else {
            stateMachine->setNextState(mainPatternState);
        }

        stateChangeTimer.startTimer(5.0f);  // Reset the timer for the next state change
    });

    stateChangeTimer.startTimer(5.0f);  // Start the timer for the first state change
}


void obelisk::ObeliskCore::tick(float deltaTime)
{
    RelicCore::tick(deltaTime);

    stateMachine->tick(deltaTime);

    stateChangeTimer.tick(deltaTime);
}

bool obelisk::ObeliskCore::handleCommand(string msg)
{
    if (strcmp(msg.c_str(), "switch") == 0)  // strcmp returns 0 if strings are equal
    {
        dbgLog("ObeliskCore::handleCommand - switching patterns", Verbosity::Display, Category::Relic);
        stateMachine->setNextState(mainPatternState);
        return true;
    }

    return RelicCore::handleCommand(msg); // Call the base class method to handle any other commands
}
