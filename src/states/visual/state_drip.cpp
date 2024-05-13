// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_drip.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include <bits/stdc++.h>

DropInfo::DropInfo(j::HSV inColor, float inDropSize) : color(inColor), dropSize(inDropSize)
{
}

State_Drip::State_Drip(const char* InStateName) : State(InStateName)
{

}

void State_Drip::onStateBegin()
{
    State::onStateBegin();

    timeSinceDrop = 0;
    nextDropTime = get_random_float_in_range(dropDelay.first, dropDelay.second);
    //todo reset drops
}

void State_Drip::tickScreen()
{
    State::tickScreen();

    GameManager& GM = GameManager::get();

    GM.Screen->drawRect(0,0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x0000);
}

void State_Drip::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    timeSinceDrop += GetLastFrameDelta().count();

    if(timeSinceDrop > nextDropTime)
    {
        nextDropTime = get_random_float_in_range(dropDelay.first, dropDelay.second);
        j::HSV color;
        switch(colorPickMode)
        {
            case ColorPickMode::RandomPalette:
                color = palette.getColor(GetStateActiveDuration().count());
                break;
            default:
                //nothing else
                break;
        }
        
        float myDropSize = get_random_float_in_range(dropSize.first, dropSize.second);
        drops.emplace_back(DropInfo(color, myDropSize));
    }

    for (std::list<DropInfo>::iterator it=drops.begin(); it != drops.end(); ++it)
    {
        it->dropTime += GetLastFrameDelta().count();
        if(it->dropTime > dropTime)
        {
            drops.erase(it++);
            continue;
        }
    }


    for(int ledIdx = 0; ledIdx < GM.RingLEDs->getLength(); ++ledIdx)
    {
        GM.RingLEDs->setBrightness(0,0);
    }

    for(int ledIdx = 0; ledIdx < GM.OutfitLEDs->getLength(); ++ledIdx)
    {
        GM.OutfitLEDs->setBrightness(0,0);
    }

    for (std::list<DropInfo>::iterator it=drops.begin(); it != drops.end(); ++it)
    {
        float dropAlpha = it->dropTime / dropTime;
        int ledCount = std::ceil(it->dropSize);
        for(int idx = 0; idx < ledCount; ++idx)
        {
            uint16_t ledIdx = idx;
        }

        //todo implement drop display
    }
}