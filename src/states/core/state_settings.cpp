// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_settings.h"

State_Settings::State_Settings(const char* InStateName) : State(InStateName)
{

}

void State_Settings::init()
{
    State::init();
}

void State_Settings::tickScreen()
{
    State::tickScreen();
}

void State_Settings::tickLEDs()
{
    State::tickLEDs();
}

void State_Settings::tickLogic()
{
    State::tickLogic();
}
