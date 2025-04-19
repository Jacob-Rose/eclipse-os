// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_cybertoxin.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/cyber-toxin.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_CyberToxin::init()
{
}

void Pattern_CyberToxin::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

}

void Pattern_CyberToxin::render(HSVStripNode *inNode, HSV &inOutColor) const
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

State_CyberToxin::State_CyberToxin(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_CyberToxin::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)cyber_toxin, sizeof(cyber_toxin));

    std::shared_ptr<Pattern_CyberToxin> newGenerator = std::make_shared<Pattern_CyberToxin>();
    setGenerator(newGenerator);
    newGenerator->init();
}
