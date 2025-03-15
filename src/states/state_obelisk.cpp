// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_obelisk.h"

#include "../lib/ecore/math.h"
#include "../lib/ecore/logging.h"
#include "../kits/palettes.h"

#include "../imgs/eclipse.h"

using namespace eanim;
using namespace ecore;
using namespace ecore::log;

State_Obelisk_FourSeasons::State_Obelisk_FourSeasons(const char* InStateName) : State(InStateName)
{
    coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    coreNoise.noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Manhattan);
    coreNoise.noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2);
    coreNoise.noise.SetFrequency(0.05f);
    coreNoise.noise.SetCellularJitter(1.0f);
    coreNoise.timeScale = 0.33f;
    coreNoise.imageScaleX = 1.0f;
    coreNoise.imageScaleY = 1.0f;
}

void State_Obelisk_FourSeasons::onStateBegin()
{
    State::onStateBegin();
}

void State_Obelisk_FourSeasons::tick(float deltaTime)
{
    State::tick(deltaTime);

    coreNoise.tick(deltaTime);
}

void State_Obelisk_FourSeasons::render(HSVStripSegment *segment, HSVStripNode* node)
{
    State::render(segment, node);

    HSV outColor;
    int x = 0, y = 0;
    int sideIdx;

    if(node->GetStripNodeType() == StripNodeType::MAPPED2D)
    {
        HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(node);
        x = castedNode->coord.x;
        y = castedNode->coord.y;

        sideIdx = y / 2; // two strips per side
    }
    
    float noiseAlpha = 0;//coreNoise.evaluate(x, y);
    outColor = palettes[sideIdx].getColor(noiseAlpha);

    segment->setHSV(node, outColor);
}

State_Obelisk_Theater::State_Obelisk_Theater(const char* InStateName) : State(InStateName)
{
    lfo.width = 24.f;
    lfo.speed = -1.5f;

    paletteLFO.speed = 0.1f;

}

void State_Obelisk_Theater::onStateBegin()
{
    State::onStateBegin();

    log::dbgLog("State_Obelisk_Theater::onStateBegin", log::Verbosity::Display, log::Category::State);
}

void State_Obelisk_Theater::tick(float deltaTime)
{
    State::tick(deltaTime);

    lfo.tick(deltaTime);
    paletteLFO.tick(deltaTime);
}

void State_Obelisk_Theater::render(HSVStripSegment *segment, HSVStripNode* node)
{
    State::render(segment, node);

    HSV outColor;
    int x = 0, y = 0;
    int sideIdx;

    if(node->GetStripNodeType() == StripNodeType::MAPPED2D)
    {
        HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(node);
        x = castedNode->coord.x;
        y = castedNode->coord.y;

        sideIdx = y / 2; // two strips per side
    }
    float alpha = lfo.evaluate(x);
    
    outColor = palettes[sideIdx].getColor(alpha);

    segment->setHSV(node, outColor);
}