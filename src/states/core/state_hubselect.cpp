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
    MoonGif.begin(LITTLE_ENDIAN_PIXELS);

    MoonGif.open((uint8_t *)eclipse, sizeof(eclipse), j::ScreenDrawer::GIFDraw_UpscaleScreen);
}

void State_HubSelect::tickScreen()
{
    State::tickScreen();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.renderGif(MoonGif);
}

void State_HubSelect::tickLEDs()
{
    State::tickLEDs();

    GameManager& GM = GameManager::get();

    float deltaTime = GetLastFrameDelta().count();

    lfoInchwormSpeed.tick(deltaTime);
    lfoNecklaceOuter.speed = lfoInchwormSpeed.evaluate(1.0f) * inchwormSpeed;

    lfoNecklaceOuter.tick(deltaTime);

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        GM.RingLEDs->setHSV(idx, j::HSV(0,0,0));
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        float lfo = lfoNecklaceOuter.evaluate(idx);
        float pixelBrightness = lfo;
        uint8_t brightnessByte = std::lroundf(pixelBrightness * 255);
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, j::HSV(0.2f,255U,brightnessByte));
    }
}

void State_HubSelect::tickLogic()
{
    State::tickLogic();
}
