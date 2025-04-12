// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_redrum.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/mage-spell.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_RedRum::init()
{
}

void Pattern_RedRum::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

}

void Pattern_RedRum::render(HSVStripNode *inNode, HSV &inOutColor) const
{
    if(!inNode)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is null!", Verbosity::Error);
        return;
    }

    if(inNode->GetStripNodeType() != StripNodeType::MAPPED2D)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is not a Mapped2D node!", Verbosity::Error);
        return;
    }
    HSVStripNode_Mapped2D *castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);

}

State_RedRum::State_RedRum(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_RedRum::init()
{
    State_PendantGeneric::init();

    std::shared_ptr<Pattern_RedRum> newGenerator = std::make_shared<Pattern_RedRum>();
    setGenerator(newGenerator);
    newGenerator->init();
    setStateStartGifData((uint8_t *)mage_spell, sizeof(mage_spell));
}


#if 0

State_Ritual::State_Ritual(const char* InStateName) : State(InStateName)
{

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
    float deltaTime = lastFrameDT.count();

    fireOffset.tick(deltaTime);
    lfoInchwormSpeed.tick(deltaTime);
    lfoNecklaceOuter.speed = remap(0.0f, 1.0f, -inchwormSpeed, inchwormSpeed, lfoInchwormSpeed.evaluate(1.0f));
    lfoNecklaceOuter.tick(deltaTime);

    for(int idx = 0; idx < RING_ONE_LENGTH; ++idx)
    {
        GM.RingLEDs->setHSV(idx, j::HSV(0,0,0));
    }

    for(int idx = 0; idx < RING_TWO_LENGTH; ++idx)
    {
        float lfo = lfoNecklaceOuter.evaluate(idx);
        j::HSV color = firePalette.getColor(lfo);
        GM.RingLEDs->setHSV(RING_ONE_LENGTH + idx, color);
    }

    for(int i = 0; i < GM.OutfitLEDs->getLength(); ++i)
    {
        j::HSV color = firePalette.getColor(fireOffset.evaluate(i));
        GM.OutfitLEDs->setHSV(i, color);
    }
}

#endif