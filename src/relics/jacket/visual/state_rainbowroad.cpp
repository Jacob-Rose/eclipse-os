// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_rainbowroad.h"

#include "../../../lib/ecore/logging.h"
#include "../../../imgs/kirby.h"


using namespace ecore;
using namespace ecore::log;
using namespace eio;

Pattern_Jacket_RainbowRoad::Pattern_Jacket_RainbowRoad()
{
}

void Pattern_Jacket_RainbowRoad::init()
{
    cogLFO.width = 1.0f;

    cogLFO.yOffset = 0.25f;
    cogLFO.amplitude = 0.5f;
    cogLFO.bUseEasingFunction = true;
    cogLFO.easingFunction = easing_functions::EaseInOutSine;
    cogLFO.bShouldReflect = true;

    cogSpeedLFO.width = 0.3f;
    cogSpeedLFO.yOffset = 6.0f;
    cogSpeedLFO.amplitude = 2.0f;
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

    float y = castedNode->coord.y;

    constexpr float scalar = (1.f / 7.f) * 0.3f;

    float cogLFOEval = cogLFO.evaluate(castedNode->coord.x + (castedNode->coord.y * scalar));


    HSV outColor = rainbowPalette.getColor(y / 6.0f);
    outColor.setBrightnessAlpha(clamp(outColor.getValFloat() - cogLFOEval, 0.f, 1.0f));
    outColor.setSaturationAlpha(clamp(outColor.getSatFloat() + cogLFOEval, 0.f, 1.0f));
    inOutColor = outColor;
}

State_Jacket_RainbowRoad::State_Jacket_RainbowRoad(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_Jacket_RainbowRoad::init()
{
    State_PendantGeneric::init();

    setGenerator(std::make_shared<Pattern_Jacket_RainbowRoad>());
    setStateStartGifData((uint8_t *)kirby, sizeof(kirby));
}
