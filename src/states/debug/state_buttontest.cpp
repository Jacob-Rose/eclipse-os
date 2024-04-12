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

    j::HSV color = bButtonPressed ? OnColor : OffColor;

    GlobalManager& GM = GlobalManager::get();

    for(uint16_t idx = 0; idx < GM.RingLEDs->getLength(); ++idx)
    {
        GM.RingLEDs->setHSV(idx, color);
    }
}

void State_ButtonTest::tickLogic()
{
    State::tickLogic();

    GlobalManager& GM = GlobalManager::get();

    bButtonPressed = GM.BlueButton->isPressed();

    jlog::print(std::to_string(bButtonPressed));
}

void State_ButtonTest::onStateBegin()
{
    State::onStateBegin();
}

void State_ButtonTest::tickScreen()
{
    State::tickScreen();
}