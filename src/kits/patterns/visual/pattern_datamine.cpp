// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "pattern_datamine.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/hsv.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../relics/pendant.h"
#include "../../../relics/jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_Datamine::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

    if(currentState == EDatamineInputState::Uploading)
    {
        activationSpeedRamp.setTargetValue(uploadSpeed);
    }
    else if(currentState == EDatamineInputState::Downloading)
    {
        activationSpeedRamp.setTargetValue(downloadSpeed);
    }
    else
    {
        activationSpeedRamp.setTargetValue(idleSpeed);
    }

    float newWhipSpeed = activationSpeedRamp.getValue();
    lfoWhip.speed = newWhipSpeed;
    lfoNecklaceOuter.speed = newWhipSpeed;


    lfoArm.tick(deltaTime);
    lfoNecklaceInner.tick(deltaTime);
    lfoNecklaceOuter.tick(deltaTime);
    lfoWhip.tick(deltaTime);
    activationSpeedRamp.tick(deltaTime);
}

void Pattern_Datamine::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    float newWhipSpeed = activationSpeedRamp.getValue();

    float animAlphaToUpload = ((newWhipSpeed - idleSpeed) / (uploadSpeed - idleSpeed));
    float animAlphaToDownload = (newWhipSpeed - idleSpeed) / (downloadSpeed + idleSpeed);
    bool bUsePaletteBlendToUpload = newWhipSpeed < idleSpeed - idlePaletteBuffer;
    bool bUsePaletteBlendToDownload = newWhipSpeed > idleSpeed + idlePaletteBuffer;

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
        float alphaPercent = castedNode->coord.x;
        float lfo = lfoNecklaceOuter.evaluate(castedNode->coord.x * pendant::InnerRingLength);
        //lfo = easingFunction(lfo);
        float pixelBrightness = lfo;
        HSV color = idlePalette.getColor(alphaPercent + std::abs(lfoNecklaceOuter.getCurrentOffset()) );
        color.v *= lfo;
        inOutColor = color;

        return;
    }

    else if(castedNode->getStripSegment()->getId() == (int)pendant::EPendantSegmentID::OuterRing)
    {
        float alphaPercent = castedNode->coord.x;
        //auto easingFunction = getEasingFunction( EaseInOutSine );
        float lfo = lfoNecklaceInner.evaluate(alphaPercent * pendant::OuterRingLength);
        //lfo = easingFunction(lfo);
        float pixelBrightness = lfo;

        HSV uploadColor = uploadPalette.getColor(alphaPercent + 0.25f);
        HSV downloadColor = downloadPalette.getColor(alphaPercent + 0.25f);
        HSV idleColor = idlePalette.getColor(alphaPercent + 0.25f);

        HSV newColor;
        if(bUsePaletteBlendToUpload)
        {
            newColor = uploadColor;
        }
        else if(bUsePaletteBlendToDownload)
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
        float lfo = lfoWhip.evaluate(castedNode->coord.y * jacket::MONOWIRE_LENGTH);
        float pixelBrightness = lfo;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);

        float percentThrough = 0.0f;//(float)idx / WHIP_LED_LENGTH; // TODO FIX

        HSV uploadColor = uploadPalette.getColor(percentThrough);
        HSV downloadColor = downloadPalette.getColor(percentThrough);
        HSV idleColor = idlePalette.getColor(percentThrough);

        HSV newColor;
        if(bUsePaletteBlendToUpload)
        {
            newColor = uploadColor;
        }
        else if(bUsePaletteBlendToDownload)
        {
            newColor = downloadColor;
        }
        else
        {
            newColor = idleColor;
        }

        newColor.v = ((uint16_t)newColor.v * brightnessByte) / 255;
        inOutColor = newColor;
    }
    else
    {
        float lfo2 = lfoArm.evaluate(castedNode->coord.x);
        float pixelBrightness = lfo2;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);
        HSV color = idlePalette.getColor(lfo2);
        color.v = brightnessByte;
        inOutColor = color;
    }
}