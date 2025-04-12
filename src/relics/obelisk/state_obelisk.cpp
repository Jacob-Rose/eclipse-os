// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_obelisk.h"

#include "../../lib/ecore/math.h"
#include "../../lib/ecore/logging.h"
#include "../../kits/palettes.h"

#include "../../imgs/eclipse.h"

using namespace eanim;
using namespace ecore;
using namespace ecore::log;

#define OBELISK_DEBUG_ENABLED DEBUG_LOGGING_ENABLED && 0

int getSideIndex(HSVStripNode_Mapped2D* inNode)
{
    int sideIdx = inNode->coord.x / 2; // two strips per side

#if ERROR_CHECKING_ENABLED
    if(sideIdx >= 4)
    {
        string str = "Pattern_Obelisk_Theater::render - sideIdx out of range: " + std::to_string(sideIdx) + " >= 4";
        //dbgLog(str.c_str(), Verbosity::Warning);
        return 0;
    }
#endif

    return sideIdx;
}

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
    dbgLog("four-seasons ~ start");
#endif
#if ERROR_CHECKING_ENABLED
    if(inNode->GetStripNodeType() != StripNodeType::MAPPED2D)
    {
        dbgLog("four-seasons ~ only supports mapped 2d nodes", Verbosity::Error);
    }
#endif

    HSV outColor;

    HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
    int x = castedNode->coord.x;
    int y = castedNode->coord.y;
    int sideIdx = getSideIndex(castedNode);

#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ mapped coords");
#endif

    float noiseAlpha = coreNoise.evaluate(x, y);

#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ noise");
#endif
    inOutColor = palettes[sideIdx].getColor(noiseAlpha);
}

Pattern_Obelisk_Theater::Pattern_Obelisk_Theater()
{
    lfo.width = 2.f;
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
    
    HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
    int x = castedNode->coord.x;
    int y = castedNode->coord.y;
    int sideIdx = getSideIndex(castedNode);

    float alpha = lfo.evaluate(x);
    
    inOutColor = palettes[3].getColor(alpha);
}

Pattern_Obelisk_Monocolor::Pattern_Obelisk_Monocolor()
{
}

void Pattern_Obelisk_Monocolor::render(HSVStripNode *inNode, HSV &inOutColor) const
{
    inOutColor = color;
}
