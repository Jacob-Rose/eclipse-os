// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_systemoverload.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/cyberskull-critical.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_SystemOverload::init()
{
    staticNoise.imageScaleX = 10000000;
    staticNoise.imageScaleY = 10000000;
    staticNoise.timeScale = 6.0f;

    staticNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);

    lightningNoise.imageScaleX = 100.0f;
    lightningNoise.imageScaleY = 100.0f;
    lightningNoise.timeScale = 3.0f;
}

void Pattern_SystemOverload::tick(float deltaTime)
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

    if(bStopStrobing)
    {
        staticNoise.timeScale = 1.65f;
        lightningNoise.timeScale = 1.15f;
    }
    else
    {
        staticNoise.timeScale = 8.0f;
        lightningNoise.timeScale = 3.0f;
    }

    staticNoise.tick(deltaTime);

    lightningNoise.tick(deltaTime);

}

void Pattern_SystemOverload::render(HSVStripNode *inNode, HSV &inOutColor) const
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

    float staticNoiseVal = staticNoise.evaluate(castedNode->coord.x, castedNode->coord.y);
    // staticNoiseVal = getEasingFunction(easing_functions::EaseInOutCubic)(staticNoiseVal);
    float lightningNoiseVal = lightningNoise.evaluate(castedNode->coord.x, castedNode->coord.y);

    HSV colorA = staticColor;
    colorA.setBrightnessAlpha(colorA.getValFloat() * (staticNoiseVal * 0.2f) + 0.3f);
    inOutColor = colorA;

    HSV colorB = lightningColor;
    float threshold = 0.75f;
    threshold -= buttonALerper.getValue() * 0.5f;
    if(lightningNoiseVal > threshold)
    {
        colorB.setBrightnessAlpha(colorB.getValFloat() * lightningNoiseVal);
        inOutColor = colorB;
    }
    
}

State_SystemOverload::State_SystemOverload(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_SystemOverload::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)cyberskull_critical, sizeof(cyberskull_critical));

    std::shared_ptr<Pattern_SystemOverload> newGenerator = std::make_shared<Pattern_SystemOverload>();
    setGenerator(newGenerator);
    newGenerator->init();
}

void State_SystemOverload::tick(float deltaTime)
{   
    State_PendantGeneric::tick(deltaTime);

    jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
    Pattern_SystemOverload* enchantedPattern = static_cast<Pattern_SystemOverload*>(getGenerator().get());

    enchantedPattern->bIsButtonAActive = jacketIO->getRemoteBlackButton()->isPressed();
    enchantedPattern->bIsButtonBActive = jacketIO->getRemoteWhiteButton()->isPressed();

    if(jacketIO->getRedButton()->runButtonPressedScan())
    {
        enchantedPattern->bStopStrobing = false;
    }
    
}

void State_SystemOverload::onStateChangeState(StateStatus inStatus)
{
    State_PendantGeneric::onStateChangeState(inStatus);

    if(inStatus == StateStatus::Active)
    {
        jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
        Pattern_SystemOverload* enchantedPattern = static_cast<Pattern_SystemOverload*>(getGenerator().get());

        enchantedPattern->bStopStrobing = true;
    }
}
