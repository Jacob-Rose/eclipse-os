// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_breathewithme.h"

#include "../../../lib/ecore/logging.h"

#include "../../../lib/ecore/math.h"


#include "../jacket_io.h"

#include "../../../imgs/flicker-stars.h"

using namespace ecore;
using namespace ecore::log;


void Pattern_BreatheWithMe::render(HSVStripNode *node, HSV &inOutColor) const
{
    if(!node)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is null!", Verbosity::Error);
        return;
    }

    if(node->GetStripNodeType() != StripNodeType::MAPPED2D)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is not a Mapped2D node!", Verbosity::Error);
        return;
    }

    HSVStripNode_Mapped2D *castedNode = static_cast<HSVStripNode_Mapped2D*>(node);

    float alpha = lfo1.evaluate(castedNode->coord.x + (castedNode->coord.y * 0.4f)) + lfo2.evaluate(castedNode->coord.x);
    alpha /= 2.0f;

    inOutColor = palette.getColor(alpha);

    float hueShift = buttonALerper.getValue() * 120.0f;
    inOutColor.setHueDegree(inOutColor.getHueFloat() + hueShift);

    float sat = (buttonALerper.getValue() * 0.6f + 0.4f) * inOutColor.getSatFloat();
    inOutColor.setSaturationAlpha(sat);

    float brightnessScalar = lerp(0.4f, 1.0f, buttonALerper.getValue());
    inOutColor.setBrightnessAlpha(inOutColor.getValFloat() * brightnessScalar);
}


void Pattern_BreatheWithMe::init()
{
    lfo1.width = 0.75f;
    lfo1.amplitude = 1.0f;
    lfo1.yOffset = 0.5f;
    lfo1.speed = 0.75f;

    lfo2.width = 0.25f;
    lfo2.amplitude = 1.0f;
    lfo2.yOffset = 0.5f;
    lfo2.speed = 0.5f;

    rainbowLFO.width = 2.0f;
    rainbowLFO.speed = 1.5f;
    rainbowLFO.amplitude = 1.0f;
    rainbowLFO.yOffset = 0.5f;
}

void Pattern_BreatheWithMe::tick(float deltaTime)
{
    lfo1.tick(deltaTime);
    lfo2.tick(deltaTime);
    rainbowLFO.tick(deltaTime);

    if(bIsButtonAActive != bIsButtonAActiveLast)
    {
        bIsButtonAActiveLast = bIsButtonAActive;
        buttonALerper.startLerp(bIsButtonAActive ? 1.0f : 0.0f, 4.0f);
    }
    if(bIsButtonBActive != bIsButtonBActiveLast)
    {
        bIsButtonBActiveLast = bIsButtonBActive;
        buttonBLerper.startLerp(bIsButtonBActive ? 1.0f : 0.0f, 1.0f);
    }

    buttonALerper.tick(deltaTime);
    buttonBLerper.tick(deltaTime);
}


State_BreatheWithMe::State_BreatheWithMe(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{

}

void State_BreatheWithMe::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)flicker_stars, sizeof(flicker_stars));

    std::shared_ptr<Pattern_BreatheWithMe> pattern = std::make_shared<Pattern_BreatheWithMe>();
    pattern->init();
    setGenerator(pattern);
}

void State_BreatheWithMe::tick(float deltaTime)
{
    State_PendantGeneric::tick(deltaTime);

    jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
    Pattern_BreatheWithMe* pattern = static_cast<Pattern_BreatheWithMe*>(getGenerator().get());

    pattern->bIsButtonAActive = jacketIO->getRemoteBlackButton()->isPressed();
    pattern->bIsButtonBActive = jacketIO->getRemoteWhiteButton()->isPressed();
}