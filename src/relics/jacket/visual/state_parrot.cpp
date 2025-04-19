// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_parrot.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/skull-laser-eyes.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_Parrot::init()
{
    offset.amplitude = 0.5f;
    offset.speed = 3.0f;
    offset.yOffset = 0.5f;

    flapSpeedLFO.width = 0.25f; //overritten by buttonA
    flapSpeedLFO.amplitude = 8.0f;
    flapSpeedLFO.yOffset = -3.5f;
    flapSpeedLFO.speed = 6.5f;
}

void Pattern_Parrot::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

    buttonALerper.tick(deltaTime);
    buttonBLerper.tick(deltaTime);

    if(bIsButtonAActive != bIsButtonAActiveLast)
    {
        bIsButtonAActiveLast = bIsButtonAActive;
        buttonALerper.startLerp(bIsButtonAActive ? 1.0f : 0.0f, 0.5f);
    }
    if(bIsButtonBActive != bIsButtonBActiveLast)
    {
        bIsButtonBActiveLast = bIsButtonBActive;
        buttonBLerper.startLerp(bIsButtonBActive ? 1.0f : 0.0f, 0.5f);
    }

    offset.tick(deltaTime);
    offset.speed = 2.0f + (flapSpeedLFO.evaluate(0.0f) * buttonALerper.getValue());
    flapSpeedLFO.tick(deltaTime);

}

void Pattern_Parrot::render(HSVStripNode *inNode, HSV &inOutColor) const
{
    if(!inNode)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is null!", Verbosity::Error);
        return;
    }

    if(inNode->GetStripNodeType() != StripNodeType::MAPPED2D)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is not a Mapped2D node!", Verbosity::Error);
        return;
    }
    HSVStripNode_Mapped2D *castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);

    HSV color = palette.getColor(offset.evaluate(castedNode->coord.x));
    inOutColor = color;
}

State_Parrot::State_Parrot(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_Parrot::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)skull_laser_eyes, sizeof(skull_laser_eyes));

    std::shared_ptr<Pattern_Parrot> newGenerator = std::make_shared<Pattern_Parrot>();
    setGenerator(newGenerator);
    newGenerator->init();
}

void State_Parrot::tick(float deltaTime)
{   
    State_PendantGeneric::tick(deltaTime);

    jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
    Pattern_Parrot* enchantedPattern = static_cast<Pattern_Parrot*>(getGenerator().get());

    enchantedPattern->bIsButtonAActive = jacketIO->getRemoteBlackButton()->isPressed();
    enchantedPattern->bIsButtonBActive = jacketIO->getRemoteWhiteButton()->isPressed();
}