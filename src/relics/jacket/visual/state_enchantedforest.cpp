// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_enchantedforest.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/hsv.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../jacket_io.h"

#include "../../../imgs/enchanted-forest.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;
using namespace eanim;


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

    float alpha = lfo1.evaluate(castedNode->coord.x) + lfo2.evaluate(castedNode->coord.x + castedNode->coord.y);
    alpha /= 2.0f;

    inOutColor = palette.getColor(alpha);

    inOutColor.setHueDegree(inOutColor.getHueFloat() +  (buttonALerper.getValue() * 40.0f) * lfo2.evaluate(castedNode->coord.x));
}


void Pattern_EnchantedForest::init()
{
    lfo1.width = 0.25f; //overritten by buttonA
    lfo1.amplitude = 1.0f;
    lfo1.yOffset = 0.5f;
    lfo1.speed = 3.5f;

    lfo2.width = 0.25f;
    lfo2.amplitude = 1.0f;
    lfo2.yOffset = 0.5f;
    lfo2.speed = -1.25f;
    lfo2.bShouldReflect = true;
}

void Pattern_EnchantedForest::tick(float deltaTime)
{   
    buttonALerper.tick(deltaTime);
    buttonBLerper.tick(deltaTime);

    if(bIsButtonAActive != bIsButtonAActiveLast)
    {
        bIsButtonAActiveLast = bIsButtonAActive;
        buttonALerper.startLerp(bIsButtonAActive ? 1.0f : 0.0f, 0.5f);
    }
    if(bIsButtonBActive != bIsButtonBActiveLast)
    {
        bIsButtonBActiveLast = bIsButtonBActive;
        buttonBLerper.startLerp(bIsButtonBActive ? 1.0f : 0.0f, 0.5f);
    }

    lfo1.width = (buttonALerper.getValue() * 4.0f) + 0.2f;
    lfo1.speed = 1.5f * buttonALerper.getValue() + 2.0f;

    lfo2.width = (buttonBLerper.getValue() * 0.25f) + 0.5f;
    lfo2.speed = -10.25f * buttonBLerper.getValue() - 1.25f;

    lfo1.tick(deltaTime);
    lfo2.tick(deltaTime);
}

State_EnchantedForest::State_EnchantedForest(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{

}

void State_EnchantedForest::tick(float deltaTime)
{   
    State_PendantGeneric::tick(deltaTime);

    jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
    Pattern_EnchantedForest* enchantedPattern = static_cast<Pattern_EnchantedForest*>(getGenerator().get());

    enchantedPattern->bIsButtonAActive = jacketIO->getRemoteBlackButton()->isPressed();
    enchantedPattern->bIsButtonBActive = jacketIO->getRemoteWhiteButton()->isPressed();
}
