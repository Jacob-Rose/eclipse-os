// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_diptoblack.h"

#include "../../gm.h"

State_DipToBlack::State_DipToBlack(const char* InStateName) : State(InStateName)
{

}

void State_DipToBlack::tickScreen()
{
    State::tickScreen();
}

void State_DipToBlack::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    uint8_t brightness = static_cast<uint8_t>((GetStateActiveDuration().count() / pixelsFadeTime) * 255);

    for(int idx = 0; idx < GM.OutfitLEDs->getLength(); ++idx)
    {
        uint8_t currentBrightness = GM.OutfitLEDs->getBrightness(idx);
        uint8_t newBrightness = (currentBrightness + brightness);
        GM.OutfitLEDs->setBrightness(idx, newBrightness);
    }

    for(int idx = 0; idx < GM.RingLEDs->getLength(); ++idx)
    {
        uint8_t currentBrightness = GM.OutfitLEDs->getBrightness(idx);
        uint8_t newBrightness = (currentBrightness + brightness);
        GM.RingLEDs->setBrightness(idx, newBrightness);
    }
}

void State_DipToBlack::tickLogic()
{
    State::tickLogic();
}

void State_DipToBlack::onStateBegin()
{
    State::onStateBegin();
}