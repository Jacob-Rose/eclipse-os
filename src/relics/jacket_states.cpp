// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "jacket_states.h"

#include "../lib/ecore/math.h"
#include "../lib/ecore/logging.h"
#include "../lib/eio/strip_projection.h"
#include "../kits/palettes.h"

#include <algorithm>

using namespace ecore::log;
using namespace ecore;
using namespace eio;

using namespace jacket;

Pattern_Jacket_TheaterLFO::Pattern_Jacket_TheaterLFO()
{

}

void Pattern_Jacket_TheaterLFO::tick(float deltaTIme)
{
}

void Pattern_Jacket_TheaterLFO::render(HSVStripNode *inNode, HSV &inOutColor) const
{
}

Pattern_Jacket_ChargeHandPulse::Pattern_Jacket_ChargeHandPulse()
{
}

void Pattern_Jacket_ChargeHandPulse::tick(float deltaTime)
{
}

void Pattern_Jacket_ChargeHandPulse::render(HSVStripNode *inNode, HSV &inOutColor) const
{
}

Pattern_Jacket_MonowireDataTransfer::Pattern_Jacket_MonowireDataTransfer()
{
}

void Pattern_Jacket_MonowireDataTransfer::tick(float deltaTime)
{
}

void Pattern_Jacket_MonowireDataTransfer::render(HSVStripNode *inNode, HSV &inOutColor) const
{
}

Pattern_Jacket_WarpTurbines::Pattern_Jacket_WarpTurbines()
{
}

void Pattern_Jacket_WarpTurbines::init()
{
    turbineLFO.speed = 3.5f;
    turbineLFO.width = 0.3f;

    turbineLFO.bUseEasingFunction = true;
    turbineLFO.easingFunction = easing_functions::EaseInCubic;
}

void Pattern_Jacket_WarpTurbines::tick(float deltaTime)
{
    turbineLFO.tick(deltaTime);
}

void Pattern_Jacket_WarpTurbines::render(HSVStripNode *inNode, HSV &inOutColor) const
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

    const float x = castedNode->coord.x;
    const float y = castedNode->coord.y;

    //constexpr float scalar = (1.f / 7.f) * 0.3f;
    bool bIsTurbineA = ((int)y) % 2 == 0;

    float turbineLFOEval = turbineLFO.evaluate(x * (bIsTurbineA ? -1.f : 1.f));
    turbineLFOEval = turbineLFOEval - turbineLFO.heightOffset < 0.0f ? turbineLFOEval : turbineLFOEval - turbineLFO.heightOffset;


    HSV outColor = bIsTurbineA ? turbinePaletteA.getColor(1.0f - turbineLFOEval) : turbinePaletteB.getColor(1.0f - turbineLFOEval);
    inOutColor = outColor;

}

Pattern_Jacket_RainbowRoad::Pattern_Jacket_RainbowRoad()
{
}

void jacket::Pattern_Jacket_RainbowRoad::init()
{
    cogLFO.width = 1.0f;

    cogLFO.heightOffset = 0.25f;
    cogLFO.amplitude = 0.5f;
    cogLFO.bUseEasingFunction = true;
    cogLFO.easingFunction = easing_functions::EaseInOutSine;

    cogSpeedLFO.width = 0.3f;
    cogSpeedLFO.heightOffset = 6.0f;
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
    //bounce sine
    cogLFOEval = cogLFOEval - cogLFO.heightOffset < 0.0f ? cogLFOEval : cogLFOEval - cogLFO.heightOffset;


    HSV outColor = rainbowPalette.getColor(y / 6.0f);
    outColor.setBrightnessAlpha(clamp(outColor.getValFloat() - cogLFOEval, 0.f, 1.0f));
    outColor.setSaturationAlpha(clamp(outColor.getSatFloat() + cogLFOEval, 0.f, 1.0f));
    inOutColor = outColor;
}
