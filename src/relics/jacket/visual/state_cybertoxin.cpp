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
    coreNoise.imageScaleX = 100.0f;
    coreNoise.imageScaleY = 10.0f;
    coreNoise.timeScale = 0.4f;

    coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    coreNoise.noise.SetFractalType(FastNoiseLite::FractalType_Ridged);

    coreNoise.noise.SetFrequency(0.01f);
}

void Pattern_CyberToxin::tick(float deltaTime)
{
    GeneratorHSV::tick(deltaTime);

    coreNoise.tick(deltaTime);
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

    float alpha = coreNoise.evaluate(castedNode->coord.x, castedNode->coord.y);
    inOutColor = palette.getColor(alpha);
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

void State_CyberToxin::tick(float deltaTime)
{
    State_PendantGeneric::tick(deltaTime);
    
    jacket::JacketIO* jacketIO = static_cast<jacket::JacketIO*>(io);
    Pattern_CyberToxin* enchantedPattern = static_cast<Pattern_CyberToxin*>(getGenerator().get());

    enchantedPattern->bIsButtonAActive = jacketIO->getRemoteBlackButton()->isPressed();
    enchantedPattern->bIsButtonBActive = jacketIO->getRemoteWhiteButton()->isPressed();
}
