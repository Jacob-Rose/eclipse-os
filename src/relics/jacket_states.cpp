// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "jacket_states.h"

#include "../lib/ecore/math.h"
#include "../lib/ecore/logging.h"
#include "../lib/eio/strip_projection.h"
#include "../kits/palettes.h"

#include "jacket_io.h"

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
    turbineLFO.easingFunction = easing_functions::EaseInOutCirc;

    turbineLFO.bShouldReflect = true;


    heightLFO.width = 1.0f;
    heightLFO.amplitude = 1.0f;
    heightLFO.xOffset = 0.5f;
    heightLFO.bShouldReflect = true;
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
    float heightLFOEval = heightLFO.evaluate(y);

    if(inNode->getStripSegment()->getId() == (int)jacket::JacketSegmentID::MONOWIRE)
    {
        HSV outColorA = turbinePaletteA.getColor(0.0f);
        HSV outColorB = turbinePaletteB.getColor(0.0f);

        inOutColor = HSV::blend(outColorA, outColorB, heightLFOEval);
        return;
    }
    else 
    {
        float turbineLFOEval = turbineLFO.evaluate(x * (bIsTurbineA ? -1.f : 1.f));

        HSV outColor = bIsTurbineA ? turbinePaletteA.getColor(1.0f - turbineLFOEval) : turbinePaletteB.getColor(1.0f - turbineLFOEval);
        inOutColor = outColor;
    }
}

Pattern_Jacket_RainbowRoad::Pattern_Jacket_RainbowRoad()
{
}

void jacket::Pattern_Jacket_RainbowRoad::init()
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

void jacket::Pattern_Jacket_DigitalVoid::init()
{
    coreNoise.imageScaleX = 100.0f;
    coreNoise.imageScaleY = 100.0f;
    coreNoise.timeScale = 25.0f;

    hueShiftNoise.imageScaleX = 25.0f;
    hueShiftNoise.imageScaleY = 25.0f;
}

void jacket::Pattern_Jacket_DigitalVoid::tick(float deltaTime)
{
    coreNoise.tick(deltaTime);
    hueShiftNoise.tick(deltaTime);
}
