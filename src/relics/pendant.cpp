// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "pendant.h"

#include "../lib/eio/strip_projection.h"

using namespace eio;

pendant::PendantIO::PendantIO()
{
}

void pendant::PendantIO::init()
{
    RelicIO::init();

    screen = std::make_shared<Adafruit_GC9A01A>(ScreenCS, ScreenDC, ScreenSDA, ScreenSCL, ScreenRST);
    screen->begin();
    screen->setRotation(0);

    screenDrawer = std::make_unique<ScreenDrawer>();
    screenDrawer->setScreenRef(screen);

    int RingOneLength = 12;
    int RingTwoLength = 16;
    int RingStripLength = RingOneLength + RingTwoLength;

    int mainStripId = 5667; //magic number, id only, trying not to conflict with possible parent
    auto [mainStripIt, stripInserted] = strips.emplace(mainStripId, make_unique<HSVStrip>(RingStripLength, RingLEDPin, NEO_RBG + NEO_KHZ800));

    HSVStrip* mainStrip = mainStripIt->second.get();

    // TODO make circular mapping
    int id1 = static_cast<uint8_t>(EPendantSegmentID::InnerRing);
    auto [it1, inserted1] = strip_segments.emplace(id1, make_unique<HSVStripSegment>(mainStrip, id1));
    if (inserted1) HSVStripNodeFactory::GenerateAxisRow(it1->second.get(), 0, RingOneLength, Coord(0,0), Coord(1.f / RingOneLength, 0.0f));

    int id2 = static_cast<uint8_t>(EPendantSegmentID::OuterRing);
    auto [it2, inserted2] = strip_segments.emplace(id2, make_unique<HSVStripSegment>(mainStrip, id2));
    if (inserted2) HSVStripNodeFactory::GenerateAxisRow(it2->second.get(), RingOneLength, RingTwoLength, Coord(0,0), Coord(1.f / RingTwoLength, 1.0f));
}

void pendant::PendantIO::tick(float deltaTime)
{
    RelicIO::tick(deltaTime);
}

void pendant::PendantIO::tick2()
{
    lastFrameDT = std::chrono::system_clock::now() - tickStartTime;
    tickStartTime = std::chrono::system_clock::now();

    float deltaTime = lastFrameDT.count();
    if (screenDrawer)
    {
        screenDrawer->tick(deltaTime);
    }
}

void pendant::PendantIO::cleanup()
{
}

pendant::PendantCore::PendantCore()
{
}

void pendant::PendantCore::init()
{
    RelicCore::init();

    coreIO = std::make_unique<PendantIO>();
    coreIO->init();
}

void pendant::PendantCore::tick(float deltaTime)
{
    RelicCore::tick(deltaTime);
}

void pendant::PendantCore::tick2()
{
    if (coreIO)
    {
        pendant::PendantIO* pendantIO = static_cast<pendant::PendantIO*>(coreIO.get());
        pendantIO->tick2();
    }
}