// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_digitalvoid.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"


#include "../../../imgs/eclipse.h"

using namespace ecore;
using namespace ecore::log;

Pattern_DigitalVoid::Pattern_DigitalVoid()
{
}

void Pattern_DigitalVoid::init()
{
    coreNoise.imageScaleX = 100.0f;
    coreNoise.imageScaleY = 10.0f;
    coreNoise.timeScale = 2.0f;

    coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);

    hueShiftNoise.imageScaleX = 100.0f;
    hueShiftNoise.imageScaleY = 10.0f;
    hueShiftNoise.tick(12.0f);

    hueShiftNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    //hueShiftNoise.noise.SetFrequency(0.05f);
    //hueShiftNoise.noise.SetCellularJitter(1.0f);
}

void Pattern_DigitalVoid::tick(float deltaTime)
{
    coreNoise.tick(deltaTime);
    hueShiftNoise.tick(deltaTime);
}

void Pattern_DigitalVoid::render(HSVStripNode *inNode, HSV &inOutColor) const
{
#if ERROR_CHECKING_ENABLED
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
#endif
    HSVStripNode_Mapped2D *castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);

    float hueShift = hueShiftNoise.evaluate(castedNode->coord.x, castedNode->coord.y);
    hueShift = remap(0.0f, 1.0f, -hueShiftVariance, hueShiftVariance, hueShift);

    float coreNoiseValue = coreNoise.evaluate(castedNode->coord.x, castedNode->coord.y);

    HSV color = targetColor;

    float newBrightnessAlpha = ((color.getValFloat() * coreNoiseValue) * 0.8f) + 0.2f;
    color.setBrightnessAlpha(newBrightnessAlpha);
    color.setHueDegree(color.getHueFloat() + hueShift);

    inOutColor = color;
}

State_DigitalVoid::State_DigitalVoid(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_DigitalVoid::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)eclipse, sizeof(eclipse));

    std::shared_ptr<Pattern_DigitalVoid> pattern = std::make_shared<Pattern_DigitalVoid>();
    pattern->init();
    setGenerator(pattern);
}
