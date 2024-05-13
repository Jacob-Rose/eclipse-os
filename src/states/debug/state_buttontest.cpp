// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_buttontest.h"

#include <memory>

#include "../../lib/j/jio.h"
#include "../../gm.h"
#include "../../lib/j/jlogging.h"

State_ButtonTest::State_ButtonTest(const char* InStateName) : State(InStateName)
{

}

void State_ButtonTest::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    j::HSV currentColor = OffColor;

    if(GM.GreenButton->isPressed())
    {
        currentColor = GreenColor;
    }
    else if(GM.RedButton->isPressed())
    {
        currentColor = RedColor;
    }
    else if(GM.BlueButton->isPressed())
    {
        currentColor = BlueColor;
    }
    else if(GM.WhiteButton->isPressed())
    {
        currentColor = WhiteColor;
    }
    else if(GM.RemoteBlackButton->isPressed())
    {
        currentColor = BlackColor;
    }
    else if(GM.RemoteWhiteButton->isPressed())
    {
        currentColor = YellowColor;
    }

    for(uint16_t idx = 0; idx < GM.RingLEDs->getLength(); ++idx)
    {
        GM.RingLEDs->setHSV(idx, currentColor);
    }

    GM.BoardLED->setHSV(0, currentColor);
}