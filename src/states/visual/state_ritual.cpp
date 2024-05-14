// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_ritual.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/ritual-fast.h"

State_Ritual::State_Ritual(const char* InStateName) : State(InStateName)
{
    Size = 60;
}

void State_Ritual::onStateBegin()
{
    State::onStateBegin();
    
    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)ritual_fast, sizeof(ritual_fast));
}

void State_Ritual::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();
}