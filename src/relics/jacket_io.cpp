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
    auto [mainStripIt, stripInserted] = strips.emplace(static_cast<uint8_t>(0), make_unique<HSVStrip>(LENGTH, pendant::OutfitLEDPin));


    HSVStrip* mainStrip = mainStripIt->second.get();
    int currentPixelIdx = 0;

    int id1 = static_cast<uint8_t>(JacketSegmentID::RING_ONE);
    auto [it1, inserted1] = strip_segments.emplace(id1, make_unique<HSVStripSegment>(mainStrip, id1));
    float incrementX1 = (jacket::RING_ONE_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_ONE_LENGTH);
    float startX1Pos = jacket::RING_ONE_FORWARD ? 0.f : 1.f;
    if (inserted1) HSVStripNodeFactory::GenerateAxisRow(it1->second.get(), currentPixelIdx, jacket::RING_ONE_LENGTH, Coord(startX1Pos,0), Coord(incrementX1, 0.0f));
    currentPixelIdx += jacket::RING_ONE_LENGTH;

    int id2 = static_cast<uint8_t>(JacketSegmentID::RING_TWO);
    auto [it2, inserted2] = strip_segments.emplace(id2, make_unique<HSVStripSegment>(mainStrip, id2));
    float incrementX2 = (jacket::RING_TWO_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_TWO_LENGTH);
    float startX2Pos = jacket::RING_TWO_FORWARD ? 0.f : 1.f;
    if (inserted2) HSVStripNodeFactory::GenerateAxisRow(it2->second.get(), currentPixelIdx, jacket::RING_TWO_LENGTH, Coord(startX2Pos,1), Coord(incrementX2, 0.0f));
    currentPixelIdx += jacket::RING_TWO_LENGTH;

    int id3 = static_cast<uint8_t>(JacketSegmentID::RING_THREE);
    auto [it3, inserted3] = strip_segments.emplace(id3, make_unique<HSVStripSegment>(mainStrip, id3));
    float incrementX3 = (jacket::RING_THREE_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_THREE_LENGTH);
    float startX3Pos = jacket::RING_THREE_FORWARD ? 0.f : 1.f;
    if (inserted3) HSVStripNodeFactory::GenerateAxisRow(it3->second.get(), currentPixelIdx, jacket::RING_THREE_LENGTH, Coord(startX3Pos,2), Coord(incrementX3, 0.0f));
    currentPixelIdx += jacket::RING_THREE_LENGTH;

    int id4 = static_cast<uint8_t>(JacketSegmentID::RING_FOUR);
    auto [it4, inserted4] = strip_segments.emplace(id4, make_unique<HSVStripSegment>(mainStrip, id4));
    float incrementX4 = (jacket::RING_FOUR_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_FOUR_LENGTH);
    float startX4Pos = jacket::RING_FOUR_FORWARD ? 0.f : 1.f;
    if (inserted4) HSVStripNodeFactory::GenerateAxisRow(it4->second.get(), currentPixelIdx, jacket::RING_FOUR_LENGTH, Coord(startX4Pos,3), Coord(incrementX4, 0.0f));
    currentPixelIdx += jacket::RING_FOUR_LENGTH;

    int id5 = static_cast<uint8_t>(JacketSegmentID::RING_FIVE);
    auto [it5, inserted5] = strip_segments.emplace(id5, make_unique<HSVStripSegment>(mainStrip, id5));
    float incrementX5 = (jacket::RING_FIVE_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_FIVE_LENGTH);
    float startX5Pos = jacket::RING_FIVE_FORWARD ? 0.f : 1.f;
    if (inserted5) HSVStripNodeFactory::GenerateAxisRow(it5->second.get(), currentPixelIdx, jacket::RING_FIVE_LENGTH, Coord(startX5Pos,4), Coord(incrementX5, 0.0f));
    currentPixelIdx += jacket::RING_FIVE_LENGTH;

    int id6 = static_cast<uint8_t>(JacketSegmentID::RING_SIX);
    auto [it6, inserted6] = strip_segments.emplace(id6, make_unique<HSVStripSegment>(mainStrip, id6));
    float incrementX6 = (jacket::RING_SIX_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_SIX_LENGTH);
    float startX6Pos = jacket::RING_SIX_FORWARD ? 0.f : 1.f;
    if (inserted6) HSVStripNodeFactory::GenerateAxisRow(it6->second.get(), currentPixelIdx, jacket::RING_SIX_LENGTH, Coord(startX6Pos,5), Coord(incrementX6, 0.0f));
    currentPixelIdx += jacket::RING_SIX_LENGTH;

    int id7 = static_cast<uint8_t>(JacketSegmentID::RING_SEVEN);
    auto [it7, inserted7] = strip_segments.emplace(id7, make_unique<HSVStripSegment>(mainStrip, id7));
    float incrementX7 = (jacket::RING_SEVEN_FORWARD ? 1.f : -1.f) * (1.0f / jacket::RING_SEVEN_LENGTH);
    float startX7Pos = jacket::RING_SEVEN_FORWARD ? 0.f : 1.f;
    if (inserted7) HSVStripNodeFactory::GenerateAxisRow(it7->second.get(), currentPixelIdx, jacket::RING_SEVEN_LENGTH, Coord(startX7Pos,6.5), Coord(incrementX7, 0.0f));
    currentPixelIdx += jacket::RING_SEVEN_LENGTH;

    int id8 = static_cast<uint8_t>(JacketSegmentID::MONOWIRE);
    auto [it8, inserted8] = strip_segments.emplace(id8, make_unique<HSVStripSegment>(mainStrip, id8));
    float yMonowireOffset = -0.25f;
    float yMonowireScalar = 1.25f;
    yMonowireScalar *= 7.0f * (1.0f / MONOWIRE_LENGTH);
    float xShift = 0.1f / MONOWIRE_LENGTH;
    if (inserted8) HSVStripNodeFactory::GenerateAxisRow(it8->second.get(), currentPixelIdx, jacket::MONOWIRE_LENGTH, Coord(0.9f,7.f + yMonowireOffset), Coord(xShift, -yMonowireScalar));
    currentPixelIdx += jacket::MONOWIRE_LENGTH;

    setGlobalBrightness(EBrightness::HIGH);
}