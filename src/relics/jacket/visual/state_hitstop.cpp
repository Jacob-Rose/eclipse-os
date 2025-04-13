// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_hitstop.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/praise-man.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_Hitstop::init()
{
}

void Pattern_Hitstop::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

}

void Pattern_Hitstop::render(HSVStripNode *inNode, HSV &inOutColor) const
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

void Pattern_Hitstop::activateHitstopA()
{
    timeSinceHitActivate = 0.0f;
}

State_Hitstop::State_Hitstop(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_Hitstop::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)praise_man, sizeof(praise_man));

    std::shared_ptr<Pattern_Hitstop> newGenerator = std::make_shared<Pattern_Hitstop>();
    setGenerator(newGenerator);
    newGenerator->init();
}
