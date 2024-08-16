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

        generator->applyEffectLogic(GM.OutfitLEDs->getStripHSV());
        GM.OutfitLEDs->updateStripPixels();

        generator->applyEffectLogic(GM.RingLEDs->getStripHSV());
        GM.RingLEDs->updateStripPixels();
    }
}