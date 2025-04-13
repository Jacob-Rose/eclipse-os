// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_enchantedforest.h"

#include "../../../lib/ecore/logging.h"

#include "../../../lib/ecore/math.h"

#include "../../../imgs/enchanted-forest.h"

using namespace ecore;
using namespace ecore::log;


State_EnchantedForest::State_EnchantedForest(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{

}

void State_EnchantedForest::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)enchanted_forest, sizeof(enchanted_forest));

    std::shared_ptr<Pattern_EnchantedForest> pattern = std::make_shared<Pattern_EnchantedForest>();
    pattern->init();
    setGenerator(pattern);
}


void Pattern_EnchantedForest::render(HSVStripNode *node, HSV &inOutColor) const
{
    if(!node)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is null!", Verbosity::Error);
        return;
    }

    if(node->GetStripNodeType() != StripNodeType::MAPPED2D)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is not a Mapped2D node!", Verbosity::Error);
        return;
    }

    HSVStripNode_Mapped2D *castedNode = static_cast<HSVStripNode_Mapped2D*>(node);

    float hue = lfo1.evaluate(castedNode->coord.x) + lfo2.evaluate(castedNode->coord.x);
    hue /= 2.0f;

    inOutColor = palette.getColor(hue);
}


void Pattern_EnchantedForest::init()
{
    lfo1.width = 0.25f;
    lfo1.amplitude = 1.0f;
    lfo1.yOffset = 0.5f;
    lfo1.speed = 3.5f;

    lfo2.width = 0.25f;
    lfo2.amplitude = 1.0f;
    lfo2.yOffset = 0.5f;
    lfo2.speed = -1.5f;
}

void Pattern_EnchantedForest::tick(float deltaTime)
{
    lfo1.tick(deltaTime);
    lfo2.tick(deltaTime);
}
