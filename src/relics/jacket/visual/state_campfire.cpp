// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_campfire.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/campfire.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_Campfire::init()
{
}

void Pattern_Campfire::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

}

void Pattern_Campfire::render(HSVStripNode *inNode, HSV &inOutColor) const
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

State_Campfire::State_Campfire(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_Campfire::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)campfire, sizeof(campfire));

    std::shared_ptr<Pattern_Campfire> newGenerator = std::make_shared<Pattern_Campfire>();
    setGenerator(newGenerator);
    newGenerator->init();
}
