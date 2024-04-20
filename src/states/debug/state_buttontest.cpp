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

void State_ButtonTest::tickLEDs()
{
    State::tickLEDs();

    j::HSV color = currentColor;

    GameManager& GM = GameManager::get();

    for(uint16_t idx = 0; idx < GM.RingLEDs->getLength(); ++idx)
    {
        GM.RingLEDs->setHSV(idx, color);
    }

    GM.BoardLED->setHSV(0, color);
}

void State_ButtonTest::tickLogic()
{
    State::tickLogic();

    GameManager& GM = GameManager::get();

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
    else
    {
        currentColor = OffColor;
    }
}

void State_ButtonTest::onStateBegin()
{
    State::onStateBegin();
}

void State_ButtonTest::tickScreen()
{
    State::tickScreen();
}