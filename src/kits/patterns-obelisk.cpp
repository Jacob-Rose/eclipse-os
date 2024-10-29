#include "patterns-obelisk.h"

using namespace eanim;
using namespace ecore;

Pattern_Obelisk_RedGreenNoise::Pattern_Obelisk_RedGreenNoise()
{
    coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    coreNoise.noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Manhattan);
    coreNoise.noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2);
    coreNoise.noise.SetFrequency(0.05f);
    coreNoise.noise.SetCellularJitter(1.0f);
    coreNoise.timeScale = 0.33f;
}

void Pattern_Obelisk_RedGreenNoise::init()
{
}

void Pattern_Obelisk_RedGreenNoise::tick(float deltaTime)
{
    coreNoise.tick(deltaTime);
}

void Pattern_Obelisk_RedGreenNoise::applyEffectLogic(float x, float y, HSV& InOutColor) const
{
    HSV outColor;

    float noiseAlpha = coreNoise.evaluate(x, y);
    outColor = mainPalette.getColor(noiseAlpha);

    InOutColor = outColor;
}

void Pattern_Obelisk_FourSeasons::init()
{
}

void Pattern_Obelisk_FourSeasons::tick(float deltaTime)
{
    
}

void Pattern_Obelisk_FourSeasons::applyEffectLogic(float x, float y, HSV& InOutColor) const
{

}
