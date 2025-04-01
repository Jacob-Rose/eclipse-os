// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jacket_io.h"

#include "../lib/eio/strip_projection.h"
#include "../lib/ecore/logging.h"

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
    auto [mainStripIt, stripInserted] = strips.emplace(static_cast<uint8_t>(0), make_unique<HSVStrip>(LENGTH, 22));


    HSVStrip* mainStrip = mainStripIt->second.get();
    int currentPixelIdx = 0;

    auto [it1, inserted1] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_ONE), make_unique<HSVStripSegment>(mainStrip));
    float incrementX1 = (jacket::RING_ONE_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_ONE_LENGTH);
    float startX1Pos = jacket::RING_ONE_FORWARD ? 0.f : 1.f;
    if (inserted1) it1->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_ONE_LENGTH, Coord(startX1Pos,0), Coord(incrementX1, 0.0f)));
    currentPixelIdx += jacket::RING_ONE_LENGTH;

    auto [it2, inserted2] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_TWO), make_unique<HSVStripSegment>(mainStrip));
    float incrementX2 = (jacket::RING_TWO_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_TWO_LENGTH);
    float startX2Pos = jacket::RING_TWO_FORWARD ? 0.f : 1.f;
    if (inserted2) it2->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_TWO_LENGTH, Coord(startX2Pos,1), Coord(incrementX2, 0.0f)));
    currentPixelIdx += jacket::RING_TWO_LENGTH;

    auto [it3, inserted3] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_THREE), make_unique<HSVStripSegment>(mainStrip));
    float incrementX3 = (jacket::RING_THREE_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_THREE_LENGTH);
    float startX3Pos = jacket::RING_THREE_FORWARD ? 0.f : 1.f;
    if (inserted3) it3->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_THREE_LENGTH, Coord(startX3Pos,2), Coord(incrementX3, 0.0f)));
    currentPixelIdx += jacket::RING_THREE_LENGTH;

    auto [it4, inserted4] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_FOUR), make_unique<HSVStripSegment>(mainStrip));
    float incrementX4 = (jacket::RING_FOUR_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_FOUR_LENGTH);
    float startX4Pos = jacket::RING_FOUR_FORWARD ? 0.f : 1.f;
    if (inserted4) it4->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_FOUR_LENGTH, Coord(startX4Pos,3), Coord(incrementX4, 0.0f)));
    currentPixelIdx += jacket::RING_FOUR_LENGTH;

    auto [it5, inserted5] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_FIVE), make_unique<HSVStripSegment>(mainStrip));
    float incrementX5 = (jacket::RING_FIVE_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_FIVE_LENGTH);
    float startX5Pos = jacket::RING_FIVE_FORWARD ? 0.f : 1.f;
    if (inserted5) it5->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_FIVE_LENGTH, Coord(startX5Pos,4), Coord(incrementX5, 0.0f)));
    currentPixelIdx += jacket::RING_FIVE_LENGTH;

    auto [it6, inserted6] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_SIX), make_unique<HSVStripSegment>(mainStrip));
    float incrementX6 = (jacket::RING_SIX_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_SIX_LENGTH);
    float startX6Pos = jacket::RING_SIX_FORWARD ? 0.f : 1.f;
    if (inserted6) it6->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_SIX_LENGTH, Coord(startX6Pos,5), Coord(incrementX6, 0.0f)));
    currentPixelIdx += jacket::RING_SIX_LENGTH;

    auto [it7, inserted7] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::RING_SEVEN), make_unique<HSVStripSegment>(mainStrip));
    float incrementX7 = (jacket::RING_SEVEN_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_SEVEN_LENGTH);
    float startX7Pos = jacket::RING_SEVEN_FORWARD ? 0.f : 1.f;
    if (inserted7) it7->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::RING_SEVEN_LENGTH, Coord(startX7Pos,6.5), Coord(incrementX7, 0.0f)));
    currentPixelIdx += jacket::RING_SEVEN_LENGTH;

    auto [it8, inserted8] = strip_segments.emplace(static_cast<uint8_t>(JacketSegmentID::MONOWIRE), make_unique<HSVStripSegment>(mainStrip));
    float yMonowireOffset = -0.25f;
    float yMonowireScalar = 1.25f;
    yMonowireScalar *= 7.0f * (1.0f / MONOWIRE_LENGTH);
    if (inserted8) it8->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, currentPixelIdx, jacket::MONOWIRE_LENGTH, Coord(0.9f,7.f + yMonowireOffset), Coord(0.0, -yMonowireScalar)));
    currentPixelIdx += jacket::MONOWIRE_LENGTH;
    
    dbgLog("ObeliskIO::init finished", Verbosity::Verbose, Category::Relic);

    setGlobalBrightness(EBrightness::HIGH);
}