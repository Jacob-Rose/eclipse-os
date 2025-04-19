// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_systemoverload.h"

#include "../../../lib/ecore/math.h"
#include "../../../lib/ecore/logging.h"
#include "../../../lib/eio/relic.h"
#include "../../../lib/eio/strip_projection.h"

#include "../../../imgs/cyberskull-critical.h"


#include "../jacket_io.h"

using namespace ecore;
using namespace ecore::log;
using namespace eio;

void Pattern_SystemOverload::init()
{
}

void Pattern_SystemOverload::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

}

void Pattern_SystemOverload::render(HSVStripNode *inNode, HSV &inOutColor) const
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

State_SystemOverload::State_SystemOverload(const char *InStateName, RelicIO *inIO) : State_PendantGeneric(InStateName, inIO)
{
}

void State_SystemOverload::init()
{
    State_PendantGeneric::init();

    setStateStartGifData((uint8_t *)cyberskull_critical, sizeof(cyberskull_critical));

    std::shared_ptr<Pattern_SystemOverload> newGenerator = std::make_shared<Pattern_SystemOverload>();
    setGenerator(newGenerator);
    newGenerator->init();
}
