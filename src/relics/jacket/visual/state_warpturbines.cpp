// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_warpturbines.h"

#include "../../../lib/ecore/logging.h"
#include "../jacket_io.h"


#include "../../../imgs/mage-spell.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

Pattern_Jacket_WarpTurbines::Pattern_Jacket_WarpTurbines()
{
}

void Pattern_Jacket_WarpTurbines::init()
{
    turbineLFO.speed = 3.5f;
    turbineLFO.width = 0.3f;

    turbineLFO.bUseEasingFunction = true;
    turbineLFO.easingFunction = easing_functions::EaseInOutCirc;

    turbineLFO.bShouldReflect = true;


    heightLFO.width = 1.0f;
    heightLFO.amplitude = 1.0f;
    heightLFO.xOffset = 0.5f;
    heightLFO.bShouldReflect = true;
}

void Pattern_Jacket_WarpTurbines::tick(float deltaTime)
{
    turbineLFO.tick(deltaTime);
}

void Pattern_Jacket_WarpTurbines::render(HSVStripNode *inNode, HSV &inOutColor) const
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

    const float x = castedNode->coord.x;
    const float y = castedNode->coord.y;

    //constexpr float scalar = (1.f / 7.f) * 0.3f;
    bool bIsTurbineA = ((int)y) % 2 == 0;
    float heightLFOEval = heightLFO.evaluate(y);

    if(inNode->getStripSegment()->getId() == (int)jacket::JacketSegmentID::MONOWIRE)
    {
        HSV outColorA = turbinePaletteA.getColor(0.0f);
        HSV outColorB = turbinePaletteB.getColor(0.0f);

        inOutColor = HSV::blend(outColorA, outColorB, heightLFOEval);
        return;
    }
    else 
    {
        float turbineLFOEval = turbineLFO.evaluate(x * (bIsTurbineA ? -1.f : 1.f));

        HSV outColor = bIsTurbineA ? turbinePaletteA.getColor(1.0f - turbineLFOEval) : turbinePaletteB.getColor(1.0f - turbineLFOEval);
        inOutColor = outColor;
    }
}

State_WarpTurbines::State_WarpTurbines(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_WarpTurbines::init()
{
    State_PendantGeneric::init();

    std::shared_ptr<Pattern_Jacket_WarpTurbines> newGenerator = std::make_shared<Pattern_Jacket_WarpTurbines>();
    setGenerator(newGenerator);
    newGenerator->init();
    setStateStartGifData((uint8_t *)mage_spell, sizeof(mage_spell));
}
