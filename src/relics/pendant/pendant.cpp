// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "pendant.h"

#include <cmath>

#include "../wiring.h"

#include <eio/strip_projection.h>

using namespace eio;

pendant::PendantIO::PendantIO()
{
}

void pendant::PendantIO::init()
{
    RelicIO::init();

    greenButton = std::make_unique<Button>();
    greenButton->init(GreenButtonPin);

    redButton = std::make_unique<Button>();
    redButton->init(RedButtonPin);

    blueButton = std::make_unique<Button>();
    blueButton->init(BlueButtonPin);

    whiteButton = std::make_unique<Button>();
    whiteButton->init(WhiteButtonPin);

    remoteWhiteButton = std::make_unique<Button>();
    remoteWhiteButton->init(RemoteWhiteButtonPin);

    remoteBlackButton = std::make_unique<Button>();
    remoteBlackButton->init(RemoteBlackButtonPin);

    screen = std::make_shared<Adafruit_GC9A01A>(ScreenCS, ScreenDC, ScreenSDA, ScreenSCL, ScreenRST);
    screen->begin();
    screen->setRotation(0);

    screenDrawer = std::make_unique<ScreenDrawer>();
    screenDrawer->setScreenRef(screen);

    static_assert(wiring::kPendantRing.length == InnerRingLength + OuterRingLength, "relics/wiring.h and the pendant's ring lengths disagree");

    const uint8_t mainStripId = 1; // the outfit's own strip, when there is one, is 0
    auto [mainStripIt, stripInserted] = strips.emplace(mainStripId, make_unique<HSVStrip>(wiring::kPendantRing));

    HSVStrip* mainStrip = mainStripIt->second.get();

    // Two concentric circles, each numbered clockwise, so a look reads the
    // pendant as a ring rather than a line. Pixel 0 is at three o'clock: the
    // scanner ring's layout turned a quarter clockwise, the way the pendant
    // hangs.
    const auto mapCircle = [&](HSVStripSegment* segment, int startIdx, int length, float scale)
    {
        for (int idx = 0; idx < length; ++idx)
        {
            const float angle = 2.0f * static_cast<float>(M_PI) * idx / length
                              + 0.5f * static_cast<float>(M_PI);
            const Coord at(ringCentre.x + outerRingRadius.x * scale * std::sin(angle),
                           ringCentre.y + outerRingRadius.y * scale * std::cos(angle));
            HSVStripNodeFactory::GenerateAxisRow(segment, startIdx + idx, 1, at, Coord(0.0f, 0.0f));
        }
    };

    int id1 = static_cast<uint8_t>(EPendantSegmentID::InnerRing);
    auto [it1, inserted1] = strip_segments.emplace(id1, make_unique<HSVStripSegment>(mainStrip, id1));
    if (inserted1) mapCircle(it1->second.get(), 0, InnerRingLength, innerRingScale);

    int id2 = static_cast<uint8_t>(EPendantSegmentID::OuterRing);
    auto [it2, inserted2] = strip_segments.emplace(id2, make_unique<HSVStripSegment>(mainStrip, id2));
    if (inserted2) mapCircle(it2->second.get(), InnerRingLength, OuterRingLength, 1.0f);

    setGlobalBrightness(EBrightness::MED);
}

void pendant::PendantIO::tick(float deltaTime)
{
    RelicIO::tick(deltaTime);

    greenButton->tick(deltaTime);
    redButton->tick(deltaTime);
    blueButton->tick(deltaTime);
    whiteButton->tick(deltaTime);

    remoteWhiteButton->tick(deltaTime);
    remoteBlackButton->tick(deltaTime);
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