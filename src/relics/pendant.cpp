// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "pendant.h"
#include "../imgs/campfire.h"

#include "../lib/eio/strip_projection.h"

using namespace eio;

pendant::PendantIO::PendantIO()
{
}

void pendant::PendantIO::init()
{
    RelicIO::init();

    screenDrawer = std::make_unique<ScreenDrawer>();
    screenDrawer->setScreenRef(std::make_shared<Adafruit_GC9A01A>(ScreenSDA, ScreenSCL, ScreenDC, ScreenCS, ScreenRST));
    screenDrawer->setCanvasSize(64, 64);
    screenDrawer->setScreenGif(campfire, sizeof(campfire));

    int RingOneLength = 12;
    int RingTwoLength = 16;
    int RingStripLength = RingOneLength + RingTwoLength;

    int mainStripIdx = 2; //magic number, id only
    auto [mainStripIt, stripInserted] = strips.emplace(static_cast<uint8_t>(mainStripIdx), make_unique<HSVStrip>(RingStripLength, RingLEDPin, NEO_RBG + NEO_KHZ800));

    HSVStrip* mainStrip = mainStripIt->second.get();

    // TODO make circular mapping
    auto [it1, inserted1] = strip_segments.emplace(static_cast<uint8_t>(EPendantSegmentID::InnerRing), make_unique<HSVStripSegment>(mainStrip));
    if (inserted1) it1->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, 0, RingOneLength, Coord(0,0), Coord(0, 1.f)));

    auto [it2, inserted2] = strip_segments.emplace(static_cast<uint8_t>(EPendantSegmentID::OuterRing), make_unique<HSVStripSegment>(mainStrip));
    if (inserted2) it2->second->addNodes(HSVStripNodeFactory::GenerateAxisRow(mainStrip, RingOneLength, RingTwoLength, Coord(0,0), Coord(0, 1.f)));
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
    if (pendant::PendantIO* pendantIO = static_cast<pendant::PendantIO*>(coreIO.get()))
    {
        pendantIO->tick2();
    }
}