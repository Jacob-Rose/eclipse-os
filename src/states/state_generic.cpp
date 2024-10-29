// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_generic.h"

#include "../lib/ecore/math.h"
#include "../lib/ecore/logging.h"
#include "../gm.h"

#include <AnimatedGIF.h>

#include "../imgs/eclipse.h"

using namespace eanim;

State_Generic::State_Generic(const char* InStateName) : State(InStateName)
{
}

void State_Generic::onStateBegin()
{
    State::onStateBegin();

    GameManager& GM = GameManager::get();

    GM.screenDrawer.setScreenGif((uint8_t *)eclipse, sizeof(eclipse));
}

void State_Generic::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    float deltaTime = lastFrameDT.count();

    if(generator)
    {  
        if(Tickable* genAsTickable = dynamic_cast<Tickable*>(generator.get()))
        {
            genAsTickable->tick(deltaTime);
        }

        ecore::HSV colorBuffer;
        for(uint16_t i = 0; i < GM.OutfitLEDs->getLength(); ++i)
        {
            generator->applyEffectLogic(i, colorBuffer);
            GM.OutfitLEDs->setHSV(i, colorBuffer);
        }
        GM.OutfitLEDs->updateStripPixels();

        for(uint16_t i = 0; i < GM.RingLEDs->getLength(); ++i)
        {
            generator->applyEffectLogic(i, colorBuffer);
            GM.RingLEDs->setHSV(i, colorBuffer);
        }
        GM.RingLEDs->updateStripPixels();
    }
}