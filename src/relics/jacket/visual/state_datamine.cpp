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

    float newArmSpeed = 0.0f;//activationSpeedRamp.getValue();

    if(currentState == EDatamineInputState::Uploading)
    {
        newArmSpeed = 3.5f;
    }
    else if(currentState == EDatamineInputState::Downloading)
    {
        newArmSpeed = -2.5f;
    }
    else
    {
        newArmSpeed = 1.2f;
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
        float lfo = lfoNecklace.evaluate(castedNode->coord.x);
        //lfo = easingFunction(lfo);
        float pixelBrightness = lfo;
        HSV color = idlePalette.getColor(lfo);
        color.setBrightnessAlpha(color.getValFloat() * pixelBrightness);
        inOutColor = color;

        return;
    }

    else if(castedNode->getStripSegment()->getId() == (int)pendant::EPendantSegmentID::OuterRing)
    {
        float alphaPercent = lfoNecklace.evaluate(castedNode->coord.x);

        HSV uploadColor = uploadPalette.getColor(alphaPercent);
        HSV downloadColor = downloadPalette.getColor(alphaPercent);
        HSV idleColor = idlePalette.getColor(alphaPercent);

        HSV newColor;
        if(currentState == EDatamineInputState::Uploading)
        {
            newColor = uploadColor;
        }
        else if(currentState == EDatamineInputState::Downloading)
        {
            newColor = downloadColor;
        }
        else
        {
            newColor = idleColor;
        }

        inOutColor = newColor;
    }
    else if(castedNode->getStripSegment()->getId() == (int)jacket::JacketSegmentID::MONOWIRE)
    {
        float lfo = lfoArm.evaluate(castedNode->coord.y);
        float pixelBrightness = lfo;

        float percentThrough = 0.0f;//(float)idx / WHIP_LED_LENGTH; // TODO FIX

        HSV uploadColor = uploadPalette.getColor(lfo);
        HSV downloadColor = downloadPalette.getColor(lfo);
        HSV idleColor = idlePalette.getColor(lfo);

        HSV newColor;
        if(currentState == EDatamineInputState::Uploading)
        {
            newColor = uploadColor;
        }
        else if(currentState == EDatamineInputState::Downloading)
        {
            newColor = downloadColor;
        }
        else
        {
            newColor = idleColor;
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
    std::shared_ptr<Pattern_Datamine> newGenerator = std::make_shared<Pattern_Datamine>();
    setGenerator(newGenerator);
    newGenerator->init();
}

void State_Datamine::tick(float deltaTime)
{    
    JacketIO* jacketIO = static_cast<JacketIO*>(io);
    Pattern_Datamine* dataminePattern = static_cast<Pattern_Datamine*>(getGenerator().get());


    if(jacketIO->getBlueButton()->isPressed())
    {
        dataminePattern->currentState = EDatamineInputState::Uploading;
    }
    else if(jacketIO->getRedButton()->isPressed())
    {
        dataminePattern->currentState = EDatamineInputState::Downloading;
    }
    else
    {
        dataminePattern->currentState = EDatamineInputState::Idle;
    }
}
