// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_enchantedforest.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/enchanted-forest.h"

State_EnchantedForest::State_EnchantedForest(const char* InStateName) : State(InStateName)
{

}

void State_EnchantedForest::onStateBegin()
{
    State::onStateBegin();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)enchanted_forest, sizeof(enchanted_forest));
}

void State_EnchantedForest::tick()
{
    State::tick();
}