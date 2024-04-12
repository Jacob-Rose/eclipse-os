// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_hubselect.h"

State_HubSelect::State_HubSelect(const char* InStateName) : State(InStateName)
{

}

void State_HubSelect::init()
{
    State::init();
}

void State_HubSelect::tickScreen()
{
    State::tickScreen();
}

void State_HubSelect::tickLEDs()
{
    State::tickLEDs();
}

void State_HubSelect::tickLogic()
{
    State::tickLogic();
}
