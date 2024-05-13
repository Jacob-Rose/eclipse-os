// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_hubselect.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include <AnimatedGIF.h>

#include "../../imgs/eclipse.h"

State_HubSelect::State_HubSelect(const char* InStateName) : State(InStateName)
{

}

void State_HubSelect::init()
{
    State::init();
    img.begin(LITTLE_ENDIAN_PIXELS);

    img.open((uint8_t *)eclipse, sizeof(eclipse), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_HubSelect::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    float deltaTime = GetLastFrameDelta().count();

    lfoInchwormSpeed.tick(deltaTime);
    lfoNecklaceOuter.speed = remap(0.0f, 1.0f, -inchwormSpeed, inchwormSpeed, lfoInchwormSpeed.evaluate(1.0f));
    lfoNecklaceOuter.tick(deltaTime);
    hueSaw.tick(deltaTime);

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        GM.RingLEDs->setHSV(idx, j::HSV(0,0,0));
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        float lfo = lfoNecklaceOuter.evaluate(idx);
        j::HSV color = WhipPalette.getColor(lfo);
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, color);
    }

    for(int idx = 0; idx < ARM_LED_LENGTH; ++idx)
    {
        float alphaPercent = (float)idx / ARM_LED_LENGTH;

        alphaPercent = hueSaw.evaluate(alphaPercent);
        alphaPercent = std::fmod(alphaPercent, 1.0f);
        j::HSV color = OutfitPalette.getColor(alphaPercent);
        GM.OutfitLEDs->setHSV(idx, color);
    }

    for(int idx = 0; idx < WHIP_LED_LENGTH; ++idx)
    {
        float alphaPercent = (float)idx / WHIP_LED_LENGTH;

        float colorHue = lfoNecklaceOuter.evaluate(alphaPercent);
        colorHue = std::fmod(colorHue, 1.0f);
        j::HSV color = WhipPalette.getColor(colorHue);
        GM.OutfitLEDs->setHSV(idx + ARM_LED_LENGTH, color);
    }
}

void State_HubSelect::tickScreen()
{
    State::tickScreen();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.renderGif(img);
}