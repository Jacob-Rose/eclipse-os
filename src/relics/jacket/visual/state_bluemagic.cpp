// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_bluemagic.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/mage-spell.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_BlueMagic::init()
{
}

void Pattern_BlueMagic::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

    fireOffset.tick(deltaTime);
    lfoInchwormSpeed.tick(deltaTime);
    lfoNecklaceOuter.speed = remap(0.0f, 1.0f, -inchwormSpeed, inchwormSpeed, lfoInchwormSpeed.evaluate(1.0f));
    lfoNecklaceOuter.tick(deltaTime);
}

void Pattern_BlueMagic::render(HSVStripNode *inNode, HSV &inOutColor) const
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

    if(castedNode->getStripSegment()->getId() == (int)pendant::EPendantSegmentID::InnerRing)
    {
        inOutColor = HSV(0.0f, 0.0f, 0.0f);
        return;
    }

    if(castedNode->getStripSegment()->getId() == (int)pendant::EPendantSegmentID::OuterRing)
    {
        float lfo = lfoNecklaceOuter.evaluate(castedNode->coord.x * pendant::OuterRingLength);
        inOutColor = firePalette.getColor(lfo);
        return;
    }

    inOutColor = firePalette.getColor(fireOffset.evaluate(castedNode->coord.x));
}

State_BlueMagic::State_BlueMagic(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_BlueMagic::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)mage_spell, sizeof(mage_spell));

    std::shared_ptr<Pattern_BlueMagic> newGenerator = std::make_shared<Pattern_BlueMagic>();
    setGenerator(newGenerator);
    newGenerator->init();
}
