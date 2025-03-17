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

#define OBELISK_DEBUG_ENABLED DEBUG_LOGGING_ENABLED && 0

Pattern_Obelisk_FourSeasons::Pattern_Obelisk_FourSeasons()
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

void Pattern_Obelisk_FourSeasons::tick(float deltaTime)
{
    coreNoise.tick(deltaTime);
}

void Pattern_Obelisk_FourSeasons::render(HSVStripNode* inNode, HSV& inOutColor) const
{
#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ pre");
#endif

    HSV outColor;
    int x = 0, y = 0;
    int sideIdx;

#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ start");
#endif
    if(inNode->GetStripNodeType() == StripNodeType::MAPPED2D)
    {
        HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
        x = castedNode->coord.x;
        y = castedNode->coord.y;
#if OBELISK_DEBUG_ENABLED
        dbgLog("four-seasons ~ mapped coords");
#endif

        sideIdx = y / 2; // two strips per side
    }

    float noiseAlpha = coreNoise.evaluate(x, y);

#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ noise");
#endif
    outColor = palettes[0].getColor(noiseAlpha);

#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ outcolor");
#endif

    inOutColor = outColor;
}

Pattern_Obelisk_Theater::Pattern_Obelisk_Theater()
{
    lfo.width = 24.f;
    lfo.speed = -1.5f;

    paletteLFO.speed = 0.1f;

}

void Pattern_Obelisk_Theater::tick(float deltaTime)
{
    lfo.tick(deltaTime);
    paletteLFO.tick(deltaTime);
}

void Pattern_Obelisk_Theater::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    HSV outColor;
    int x = 0, y = 0;
    int sideIdx;

    if(inNode->GetStripNodeType() == StripNodeType::MAPPED2D)
    {
        HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
        x = castedNode->coord.x;
        y = castedNode->coord.y;

        sideIdx = y / 2; // two strips per side
    }
    float alpha = lfo.evaluate(x);
    
    outColor = palettes[sideIdx].getColor(alpha);

    inNode->setHSV(outColor);
}