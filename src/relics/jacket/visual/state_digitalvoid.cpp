// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_digitalvoid.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/hsv.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../jacket_io.h"

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
    coreNoise.timeScale = 1.6f;

    coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    coreNoise.noise.SetFrequency(0.05f);
    

    hueShiftNoise.imageScaleX = 100.0f;
    hueShiftNoise.imageScaleY = 1.0f;
    hueShiftNoise.tick(10.0f);

    buttonALerper.setTargetValue(0.0f);
    buttonBLerper.setTargetValue(0.0f);

    hueShiftNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    //hueShiftNoise.noise.SetFrequency(0.05f);
    //hueShiftNoise.noise.SetCellularJitter(1.0f);
}

void Pattern_DigitalVoid::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

    if(bIsButtonAActive != bIsButtonAActiveLast)
    {
        bIsButtonAActiveLast = bIsButtonAActive;
        buttonALerper.startLerp(bIsButtonAActive ? 1.0f : 0.0f, 0.5f);
    }
    if(bIsButtonBActive != bIsButtonBActiveLast)
    {
        bIsButtonBActiveLast = bIsButtonBActive;
        buttonBLerper.startLerp(bIsButtonBActive ? 1.0f : 0.0f, 0.7f);
    }


    coreNoise.imageScaleX = remap(0.0f, 1.0f, 100.0f, 10000.f, buttonALerper.getValue());
    coreNoise.imageScaleY = remap(0.0f, 1.0f, 6.0f, 600.0f, buttonALerper.getValue());

    //coreNoise.noise.SetFrequency(remap(0.0f, 1.0f, 0.05f, 1.0f, buttonALerper.getValue()));

    coreNoise.timeScale = remap(0.0f, 1.0f, 1.7f, 14.5f, buttonBLerper.getValue());

    coreNoise.tick(deltaTime);
    hueShiftNoise.tick(deltaTime);

    buttonALerper.tick(deltaTime);
    buttonBLerper.tick(deltaTime);

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

void State_DigitalVoid::tick(float deltaTime)
{
    State_PendantGeneric::tick(deltaTime);

    jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
    Pattern_DigitalVoid* enchantedPattern = static_cast<Pattern_DigitalVoid*>(getGenerator().get());

    enchantedPattern->bIsButtonAActive = jacketIO->getRemoteBlackButton()->isPressed();
    enchantedPattern->bIsButtonBActive = jacketIO->getRemoteWhiteButton()->isPressed();
}
