// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_drip.h"

#include "../../lib/j/jmath.h"
#include "../../gm.h"

#include "../../imgs/campfire.h"

#include <bits/stdc++.h>
#include <cmath>

DropInfo::DropInfo(j::HSV inColor, float inDropSize, int inLocation) : color(inColor), dropSize(inDropSize), location(inLocation)
{
}

State_Drip::State_Drip(const char* InStateName) : State(InStateName)
{

}

void State_Drip::onStateBegin()
{
    State::onStateBegin();

    GameManager& GM = GameManager::get();

    GM.ScreenDrawer.setScreenGif((uint8_t *)campfire, sizeof(campfire));
    

    for(int ledIdx = 0; ledIdx < GM.RingLEDs->getLength(); ++ledIdx)
    {
        GM.RingLEDs->setHSV(ledIdx, j::HSV(0,0,0));
    }

    for(int ledIdx = 0; ledIdx < GM.OutfitLEDs->getLength(); ++ledIdx)
    {
        GM.OutfitLEDs->setHSV(ledIdx, j::HSV(0,0,0));
    }

    timeSinceDrop = 0;
    nextDropTime = get_random_float_in_range(dropDelay.first, dropDelay.second);
    //todo reset drops
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
        color = palette.getColor(std::fmod(GetStateActiveDuration().count(), 1.0f));
        
        float myDropSize = get_random_float_in_range(dropSize.first, dropSize.second);
        int dropIdx = random(GM.OutfitLEDs->getLength());
        drops.emplace_back(DropInfo(color, myDropSize, dropIdx));

        timeSinceDrop = 0.0f;
    }

    // Use a while loop to correctly handle iterator invalidation
    for (auto it = drops.begin(); it != drops.end(); /* no increment here */)
    {
        it->dropTime += GetLastFrameDelta().count();
        if (it->dropTime > dropTime)
        {
            it = drops.erase(it); // erase returns the next iterator
        }
        else
        {
            ++it; // increment only if not erasing
        }
    }


    for (std::list<DropInfo>::iterator it=drops.begin(); it != drops.end(); ++it)
    {
        float dropAlpha = it->dropTime / dropTime;
        int ledCount = std::ceil(it->dropSize);

        float alpha = lerp_keyframes(dropAlpha, DRIP_ALPHA_LERP);
        for(int idx = 0; idx < ledCount; ++idx)
        {
            uint16_t ledIdx = idx + it->location;
            if(ledIdx >= GM.OutfitLEDs->getLength())
            {
                break;
            }

            j::HSV color = it->color;
            color.v *= alpha;

            j::HSV currentColor = GM.OutfitLEDs->getHSV(ledIdx);
            currentColor.blendWith(color, alpha);
            GM.OutfitLEDs->setHSV(ledIdx, currentColor);
            if(ledIdx < RING_ONE_LENGTH)
            {
                GM.RingLEDs->setHSV(ledIdx, currentColor);
            }
        }
    }
}