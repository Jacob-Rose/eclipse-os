// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_rainbowroad.h"

#include "../../../lib/ecore/logging.h"
#include "../../../imgs/kirby.h"

#include "../jacket_io.h"


using namespace ecore;
using namespace ecore::log;
using namespace eio;

Pattern_Jacket_RainbowRoad::Pattern_Jacket_RainbowRoad()
{
}

void Pattern_Jacket_RainbowRoad::init()
{
    cogLFO.width = 0.50f;

    cogLFO.yOffset = 0.75f;
    cogLFO.amplitude = 0.5f;

    hueLFO.width = 0.50f;

    hueLFO.yOffset = 0.25f;
    hueLFO.amplitude = 0.5f;

    cogSpeedLFO.width = 0.3f;
    cogSpeedLFO.yOffset = 2.0f;
    cogSpeedLFO.amplitude = 6.0f;
    cogSpeedLFO.speed = 0.5f;
    cogLFO.speed = 4.5f;
}

void Pattern_Jacket_RainbowRoad::tick(float deltaTime)
{
    cogLFO.speed = cogSpeedLFO.evaluate(0.0f);
    hueLFO.speed = cogSpeedLFO.evaluate(0.0f);

    
    if(bIsButtonAActive != bIsButtonAActiveLast)
    {
        bIsButtonAActiveLast = bIsButtonAActive;
        buttonALerper.startLerp(bIsButtonAActive ? 1.0f : 0.0f, 0.1f);
    }
    if(bIsButtonBActive != bIsButtonBActiveLast)
    {
        bIsButtonBActiveLast = bIsButtonBActive;
        buttonBLerper.startLerp(bIsButtonBActive ? 1.0f : 0.0f, 1.0f);
    }

    cogLFO.tick(deltaTime);
    hueLFO.tick(deltaTime);
    cogSpeedLFO.tick(deltaTime);

    buttonALerper.tick(deltaTime);
    buttonBLerper.tick(deltaTime);
}

void Pattern_Jacket_RainbowRoad::render(HSVStripNode *inNode, HSV &inOutColor) const
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

    float cogLFOEval = cogLFO.evaluate(castedNode->coord.x + (castedNode->coord.y * 0.1f));
    float satCogLFOEval = hueLFO.evaluate(castedNode->coord.x + (castedNode->coord.y * 0.1f));

    HSV outColor;
    if(castedNode->getStripSegment()->getId() == (int)pendant::EPendantSegmentID::InnerRing)
    {
        outColor = HSV(0.0f, 0.0f, 0.0f);
    }
    else if(castedNode->getStripSegment()->getId() == (int)pendant::EPendantSegmentID::OuterRing)
    {
        outColor = rainbowPalette.getColor(castedNode->coord.x);
    }
    else
    {
        outColor = rainbowPalette.getColor(castedNode->coord.y / 6.0f);
    }

    //outColor.setBrightnessAlpha(outColor.getValFloat() * (cogLFOEval * (1.0f - buttonALerper.getValue()) ));
    outColor.setHueDegree(outColor.getHueFloat() + (satCogLFOEval * 60.0f));
    inOutColor = outColor;
}

State_Jacket_RainbowRoad::State_Jacket_RainbowRoad(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_Jacket_RainbowRoad::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)kirby, sizeof(kirby));

    std::shared_ptr<Pattern_Jacket_RainbowRoad> newGenerator = std::make_shared<Pattern_Jacket_RainbowRoad>();
    setGenerator(newGenerator);
    newGenerator->init();
}

void State_Jacket_RainbowRoad::tick(float deltaTime)
{
    State_PendantGeneric::tick(deltaTime);

    jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
    Pattern_Jacket_RainbowRoad* pattern = static_cast<Pattern_Jacket_RainbowRoad*>(getGenerator().get());

    pattern->bIsButtonAActive = jacketIO->getRemoteBlackButton()->isPressed();
    pattern->bIsButtonBActive = jacketIO->getRemoteWhiteButton()->isPressed();
}
