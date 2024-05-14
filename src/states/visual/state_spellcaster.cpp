// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_spellcaster.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/spellbook.h"

State_Spellcaster::State_Spellcaster(const char* InStateName) : State(InStateName)
{

}

void State_Spellcaster::onStateBegin()
{
    State::onStateBegin();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)spellbook, sizeof(spellbook));
}

void State_Spellcaster::tick()
{
    State::tick();
}