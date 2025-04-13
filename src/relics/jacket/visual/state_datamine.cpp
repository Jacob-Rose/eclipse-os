// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_datamine.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/hsv.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/cyberpunk.h"

#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

using namespace jacket;

void Pattern_Datamine::init()
{
    lfoNecklace.amplitude = 1.0f;
    lfoNecklace.yOffset = 0.5f;
    lfoNecklace.width = 0.25f;

    lfoArm.amplitude = 1.0f;
    lfoArm.yOffset = 0.5f;
    lfoArm.width = 0.6f;
}

void Pattern_Datamine::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

    

    if(currentState != lastInputState)
    {
        if(currentState == EDatamineInputState::Idle)
        {
            activationSpeedRamp.startLerp(0.0f, 0.4f);
        }
        else if(currentState == EDatamineInputState::Uploading)
        {
            activationSpeedRamp.startLerp(1.0f, 0.4f);
        }
        else if(currentState == EDatamineInputState::Downloading)
        {
            activationSpeedRamp.startLerp(-1.0f, 0.4f);
        }
        lastInputState = currentState;
    }

    currentActivationAmount = activationSpeedRamp.getValue();
    float newArmSpeed;
    if(currentActivationAmount > 0.0f)
    {
        newArmSpeed = remap(0.0f, 1.0f, idleSpeed, uploadSpeed, currentActivationAmount);
    }
    else
    {
        newArmSpeed = remap(-1.0f, 0.0f, downloadSpeed, idleSpeed, currentActivationAmount);
    }

    lfoArm.speed = newArmSpeed;
    lfoNecklace.speed = newArmSpeed;


    lfoArm.tick(deltaTime);
    lfoNecklace.tick(deltaTime);
    activationSpeedRamp.tick(deltaTime);
}

void Pattern_Datamine::render(HSVStripNode* inNode, HSV& inOutColor) const
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

    if(castedNode->getStripSegment()->getId() == (int)pendant::EPendantSegmentID::InnerRing)
    {
        float lfo = lfoNecklace.evaluate(castedNode->coord.x * 4.0f);
        float pixelBrightness = lfo;

        HSV newColor;
        if(currentActivationAmount > 0.0f)
        {
            HSV uploadColor = uploadPalette.getColor(lfo);
            HSV idleColor = idlePalette.getColor(lfo);
            newColor = HSV::blend(idleColor, uploadColor, currentActivationAmount);
        }
        else
        {
            HSV downloadColor = downloadPalette.getColor(lfo);
            HSV idleColor = idlePalette.getColor(lfo);
            newColor = HSV::blend(idleColor, downloadColor, std::abs(currentActivationAmount));
        }

        newColor.setBrightnessAlpha(pixelBrightness);
        inOutColor = newColor;
    }
    else if(castedNode->getStripSegment()->getId() == (int)jacket::JacketSegmentID::MONOWIRE)
    {
        float lfo = lfoArm.evaluate(castedNode->coord.y);
        float pixelBrightness = lfo;

        HSV newColor;
        if(currentActivationAmount > 0.0f)
        {
            HSV uploadColor = uploadPalette.getColor(lfo);
            HSV idleColor = idlePalette.getColor(lfo);
            newColor = HSV::blend(idleColor, uploadColor, currentActivationAmount);
        }
        else
        {
            HSV downloadColor = downloadPalette.getColor(lfo);
            HSV idleColor = idlePalette.getColor(lfo);
            newColor = HSV::blend(idleColor, downloadColor, std::abs(currentActivationAmount));
        }

        newColor.setBrightnessAlpha(pixelBrightness);
        inOutColor = newColor;
    }
    else
    {
        float lfo2 = lfoArm.evaluate(castedNode->coord.y);
        HSV color = idlePalette.getColor(lfo2);
        inOutColor = color;
    }
}

void State_Datamine::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)cyberpunk, sizeof(cyberpunk));

    std::shared_ptr<Pattern_Datamine> newGenerator = std::make_shared<Pattern_Datamine>();
    setGenerator(newGenerator);
    newGenerator->init();
}

void State_Datamine::tick(float deltaTime)
{   
    State_PendantGeneric::tick(deltaTime);

    JacketIO* jacketIO = static_cast<JacketIO*>(io);
    Pattern_Datamine* dataminePattern = static_cast<Pattern_Datamine*>(getGenerator().get());


    if(jacketIO->getRemoteBlackButton()->isPressed())
    {
        dataminePattern->currentState = EDatamineInputState::Uploading;
    }
    else if(jacketIO->getRemoteWhiteButton()->isPressed())
    {
        dataminePattern->currentState = EDatamineInputState::Downloading;
    }
    else
    {
        dataminePattern->currentState = EDatamineInputState::Idle;
    }
}
