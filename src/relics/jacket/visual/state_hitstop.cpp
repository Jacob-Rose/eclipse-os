// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_hitstop.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/praise-man.h"
#include "../../../imgs/winking-skull.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_Hitstop::init()
{
    brightnessLFO.amplitude = 0.5f;
    brightnessLFO.yOffset = 0.75f;
    brightnessLFO.width = 2.2f;
    brightnessLFO.speed = 2.75f;

    colorLFO.amplitude = 1.0f;
    colorLFO.yOffset = 0.5f;
    colorLFO.width = 0.8578f;
    colorLFO.speed = 1.42f;
}

void Pattern_Hitstop::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

    timeSinceHitActivate += deltaTime;

    brightnessLFO.tick(deltaTime);
    colorLFO.tick(deltaTime);
}

void Pattern_Hitstop::render(HSVStripNode *inNode, HSV &inOutColor) const
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

    float brightnessLFOEval = brightnessLFO.evaluate(castedNode->coord.y);
    float colorLFOEval = colorLFO.evaluate(castedNode->coord.y);

    float waveEval = timeSinceHitActivate;
    float waveHitstopFlashAlpha = std::max(1.0f - timeSinceHitActivate, 0.0f);

    
    if((waveEval > 0.0f && waveEval < 0.6f) || (waveEval > 0.8f && waveEval < 1.05f))
    {
        inOutColor = hitColor;
        //inOutColor.setBrightnessAlpha(pixelBrightness * inOutColor.getValFloat());
    }
    else
    {
        float remover = waveHitstopFlashAlpha * 0.4f;
        float pixelBrightness = brightnessLFOEval * 0.5f - remover;
        inOutColor = mainPalette.getColor(colorLFOEval);
        inOutColor.setBrightnessAlpha(pixelBrightness * inOutColor.getValFloat());
    }
}

void Pattern_Hitstop::activateHitstopA()
{
    timeSinceHitActivate = 0.0f;
}

State_Hitstop::State_Hitstop(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_Hitstop::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)winking_skull, sizeof(winking_skull));

    std::shared_ptr<Pattern_Hitstop> newGenerator = std::make_shared<Pattern_Hitstop>();
    setGenerator(newGenerator);
    newGenerator->init();
}


void State_Hitstop::tick(float deltaTime)
{
    State_PendantGeneric::tick(deltaTime);

    jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
    if(!jacketIO)
    {
        return;
    }

    if(jacketIO->getRemoteWhiteButton()->runButtonPressedScan())
    {
        std::shared_ptr<Pattern_Hitstop> pattern = std::static_pointer_cast<Pattern_Hitstop>(getGenerator());
        if(pattern)
        {
            pattern->activateHitstopA();
        }
    }
}