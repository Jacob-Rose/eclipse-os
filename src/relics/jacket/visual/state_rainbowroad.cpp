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
    cogLFO.width = 1.0f;

    cogLFO.yOffset = 0.5f;
    cogLFO.amplitude = 1.0f;
    cogLFO.bUseEasingFunction = true;
    cogLFO.easingFunction = easing_functions::EaseInOutExpo;

    cogSpeedLFO.width = 0.3f;
    cogSpeedLFO.yOffset = 6.0f;
    cogSpeedLFO.amplitude = 3.0f;
    cogLFO.speed = 1.5f;
}

void Pattern_Jacket_RainbowRoad::tick(float deltaTime)
{
    cogLFO.speed = cogSpeedLFO.evaluate(0.0f);

    cogLFO.tick(deltaTime);
    cogSpeedLFO.tick(deltaTime);
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
    float satCogLFOEval = cogLFO.evaluate(castedNode->coord.x + (castedNode->coord.y * 0.1f) + 0.5f);

    HSV outColor;
    if(castedNode->getStripSegment()->getId() == (int)pendant::EPendantSegmentID::InnerRing)
    {
        outColor = HSV(360.0f * castedNode->coord.x, 0.0f, 0.0f);
    }
    else
    {
        outColor = rainbowPalette.getColor(castedNode->coord.y / 6.0f);
    }


     
    outColor.setBrightnessAlpha(clamp(outColor.getValFloat() * cogLFOEval, 0.f, 1.0f));
    outColor.setSaturationAlpha(clamp(outColor.getSatFloat() * satCogLFOEval, 0.f, 1.0f));
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
