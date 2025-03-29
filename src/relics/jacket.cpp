// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jacket.h"

#include "../lib/eio/strip_projection.h"
#include "../lib/ecore/logging.h"

#include "../states/state_obelisk.h"

using namespace jacket;
using namespace eio;
using namespace ecore;
using namespace ecore::log;

JacketIO::JacketIO()
{
}

void JacketIO::init()
{
    PendantIO::init();

    int LENGTH = jacket::RING_ONE_LENGTH + jacket::RING_TWO_LENGTH + jacket::RING_THREE_LENGTH + jacket::RING_FOUR_LENGTH + jacket::RING_FIVE_LENGTH + jacket::RING_SIX_LENGTH + jacket::RING_SEVEN_LENGTH + jacket::MONOWIRE_LENGTH;
    //TODO Replace pin #22
    auto [mainStripIt, stripInserted] = strips.emplace(static_cast<uint8_t>(0), make_unique<HSVStrip>(LENGTH, 7));


    HSVStrip* mainStrip = mainStripIt->second.get();
    int currentPixelIdx = 0;

    float yCoordScale = 3.0f;

    auto [it1, inserted1] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_ONE), make_unique<HSVStripSegment>(mainStrip));
    float increment1 = jacket::RING_ONE_FORWARD ? 1.f : -1.f;
    if (inserted1) it1->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_ONE_LENGTH, Coord(0,0), Coord(increment1, yCoordScale * 0.0f)));
    currentPixelIdx += jacket::RING_ONE_LENGTH;

    auto [it2, inserted2] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_TWO), make_unique<HSVStripSegment>(mainStrip));
    float increment2 = jacket::RING_TWO_FORWARD ? 1.f : -1.f;
    if (inserted2) it2->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_TWO_LENGTH, Coord(0,0), Coord(increment2, yCoordScale * 1.0f)));
    currentPixelIdx += jacket::RING_TWO_LENGTH;

    auto [it3, inserted3] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_THREE), make_unique<HSVStripSegment>(mainStrip));
    float increment3 = jacket::RING_THREE_FORWARD ? 1.f : -1.f;
    if (inserted3) it3->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_THREE_LENGTH, Coord(0,0), Coord(increment3, yCoordScale * 2.0f)));
    currentPixelIdx += jacket::RING_THREE_LENGTH;

    auto [it4, inserted4] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_FOUR), make_unique<HSVStripSegment>(mainStrip));
    float increment4 = jacket::RING_FOUR_FORWARD ? 1.f : -1.f;
    if (inserted4) it4->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_FOUR_LENGTH, Coord(0,0), Coord(increment4, yCoordScale * 3.0f)));
    currentPixelIdx += jacket::RING_FOUR_LENGTH;

    auto [it5, inserted5] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_FIVE), make_unique<HSVStripSegment>(mainStrip));
    float increment5 = jacket::RING_FIVE_FORWARD ? 1.f : -1.f;
    if (inserted5) it5->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_FIVE_LENGTH, Coord(0,0), Coord(increment5, yCoordScale * 4.0f)));
    currentPixelIdx += jacket::RING_FIVE_LENGTH;

    auto [it6, inserted6] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_SIX), make_unique<HSVStripSegment>(mainStrip));
    float increment6 = jacket::RING_SIX_FORWARD ? 1.f : -1.f;
    if (inserted6) it6->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_SIX_LENGTH, Coord(0,0), Coord(increment6, yCoordScale * 5.0f)));
    currentPixelIdx += jacket::RING_SIX_LENGTH;

    auto [it7, inserted7] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_SEVEN), make_unique<HSVStripSegment>(mainStrip));
    float increment7 = jacket::RING_SEVEN_FORWARD ? 1.f : -1.f;
    if (inserted7) it7->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_SEVEN_LENGTH, Coord(0,0), Coord(increment7, yCoordScale * 6.0f)));
    currentPixelIdx += jacket::RING_SEVEN_LENGTH;

    auto [it8, inserted8] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::MONOWIRE), make_unique<HSVStripSegment>(mainStrip));
    float increment8 = (yCoordScale * 7) / jacket::MONOWIRE_LENGTH; // MONOWIRE is always forward

    float yMonowireOffset = 0.0f; // TODO - calibrate this to be the length of the arm
    float yMonowireScalar = 1.0f; // TODO - calibrate this to be the same as the last ring, or something close to it
    if (inserted8) it8->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::MONOWIRE_LENGTH, Coord(0,0), Coord(0.0, yMonowireOffset + (increment8 * yMonowireScalar))));
    currentPixelIdx += jacket::MONOWIRE_LENGTH;
    
    dbgLog("ObeliskIO::init finished", Verbosity::Verbose, Category::Relic);

    setGlobalBrightness(EBrightness::HIGH);
}

void jacket::JacketIO::tick(float deltaTime)
{
    PendantIO::tick(deltaTime);
}

JacketCore::JacketCore()
{
}

void jacket::JacketCore::init()
{
    PendantCore::init();
    
    coreIO = std::make_unique<JacketIO>();
    JacketIO* jacketIO = static_cast<JacketIO*>(coreIO.get());
    jacketIO->init();

    stateMachine = std::make_unique<StateMachine_GenericHSV>();
    stateMachine->setRelicIO(jacketIO);

    mainPatternState = std::make_shared<State_GenericHSV>("mainState", jacketIO);
    mainPatternState->setGenerator(make_shared<Pattern_Obelisk_FourSeasons>());
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
