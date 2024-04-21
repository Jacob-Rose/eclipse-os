// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_datamine.h"

#include "../../gm.h"
#include "../../lib/j/jcolors.h"
#include "../../lib/j/jmath.h"
#include "../../lib/j/jlogging.h"

#include "../../imgs/cyberpunk.h"


State_Datamine::State_Datamine(const char* InStateName) : State(InStateName)
{
}

void State_Datamine::init()
{
    State::init();

    gif.begin(LITTLE_ENDIAN_PIXELS);

    gif.open((uint8_t *)cyberpunk, sizeof(cyberpunk), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_Datamine::tickScreen()
{
    State::tickScreen();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.renderGif(gif);
}

void State_Datamine::tickLEDs()
{
    State::tickLEDs();

    GameManager& GM = GameManager::get();

    float deltaTime = GetLastFrameDelta().count();

    lfoArm.tick(deltaTime);
    lfoNecklaceInner.tick(deltaTime);
    lfoNecklaceOuter.tick(deltaTime);
    lfoWhip.tick(deltaTime);

    

    jlog::print(std::to_string(GM.WhiteButton->getTimeSinceStateChange()));

    if(GM.RedButton->isPressed() || GM.RemoteBlackButton->isPressed())
    {
        activationSpeedRamp.targetPosition = uploadSpeed;
    }
    else if(GM.BlueButton->isPressed() || GM.RemoteWhiteButton->isPressed())
    {
        activationSpeedRamp.targetPosition = downloadSpeed;
    }
    else
    {
        activationSpeedRamp.targetPosition = idleSpeed;
    }

    activationSpeedRamp.tick(deltaTime);

    float newWhipSpeed = activationSpeedRamp.getCurrentPosition();
    lfoWhip.speed = newWhipSpeed;
    lfoNecklaceOuter.speed = newWhipSpeed;

    float animAlphaToUpload = ((newWhipSpeed - idleSpeed) / (uploadSpeed - idleSpeed));
    float animAlphaToDownload = (newWhipSpeed - idleSpeed) / (downloadSpeed + idleSpeed);
    bool bUsePaletteBlendToUpload = newWhipSpeed < idleSpeed - idlePaletteBuffer;
    bool bUsePaletteBlendToDownload = newWhipSpeed > idleSpeed + idlePaletteBuffer;

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        float alphaPercent = (float)(idx) / RING_ONE_LENGTH;
        //auto easingFunction = getEasingFunction( EaseInOutSine );
        float lfo = lfoNecklaceInner.evaluate(idx);
        //lfo = easingFunction(lfo);
        float pixelBrightness = lfo;

        j::HSV uploadColor = uploadPalette.getColor(alphaPercent + 0.25f);
        j::HSV downloadColor = downloadPalette.getColor(alphaPercent + 0.25f);
        j::HSV idleColor = idlePalette.getColor(alphaPercent + 0.25f);

        j::HSV newColor;
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

        GM.RingLEDs->setHSV(idx, newColor);
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        float alphaPercent = (float)(idx + 1) / RING_TWO_LENGTH;
        float lfo = lfoNecklaceOuter.evaluate(idx);
        //lfo = easingFunction(lfo);
        float pixelBrightness = lfo;
        j::HSV color = idlePalette.getColor(alphaPercent + std::abs(lfoNecklaceOuter.getCurrentOffset()) );
        color.v *= lfo;
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, color);
    }

    for(int idx = 0; idx < ARM_LED_LENGTH; ++idx)
    {
        float lfo2 = lfoArm.evaluate(idx);
        float pixelBrightness = lfo2;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);
        j::HSV color = idlePalette.getColor(lfo2);
        color.v = brightnessByte;
        GM.OutfitLEDs->setHSV(idx, color);
    }

    for(int idx = 0; idx < WHIP_LED_LENGTH; ++idx)
    {
        float lfo = lfoWhip.evaluate(idx);
        float pixelBrightness = lfo;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);

        float percentThrough = (float)idx / WHIP_LED_LENGTH;

        j::HSV uploadColor = uploadPalette.getColor(percentThrough);
        j::HSV downloadColor = downloadPalette.getColor(percentThrough);
        j::HSV idleColor = idlePalette.getColor(percentThrough);

        j::HSV newColor;
        if(bUsePaletteBlendToUpload)
        {
            //j::HSVPalette palette = {idleColor, uploadColor};
            //newColor = palette.getColor(animAlphaToDownload);
            if(idx == 1)
            {
                jlog::print(std::to_string(animAlphaToDownload));
            }
            newColor = uploadColor;
        }
        else if(bUsePaletteBlendToDownload)
        {
            //j::HSVPalette palette = {idleColor, downloadColor};
            //newColor = palette.getColor(animAlphaToUpload);
            if(idx == 1)
            {
                jlog::print(std::to_string(animAlphaToUpload));
            }
            newColor = downloadColor;
        }
        else
        {
            newColor = idleColor;
        }

        newColor.v = ((uint16_t)newColor.v * brightnessByte) / 255;
        GM.OutfitLEDs->setHSV(idx + ARM_LED_LENGTH, newColor);
    }
}

void State_Datamine::tickLogic()
{
    State::tickLogic();

    GameManager& GM = GameManager::get();

}

void State_Datamine::onStateBegin()
{
    State::onStateBegin();
}