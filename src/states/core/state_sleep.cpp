// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "state_sleep.h"

State_Sleep::State_Sleep(const char* InStateName) : State(InStateName)
{

}

void State_Sleep::tickScreen()
{
    State::tickScreen();
}

void State_Sleep::tickLEDs()
{
    State::tickLEDs();
}

void State_Sleep::tickLogic()
{
    State::tickLogic();
}