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

    for(uint8_t sideIdx = 0; sideIdx < 4; ++sideIdx)
    {
        for(uint8_t sideColumnIdx = 0; sideColumnIdx < 2; ++sideColumnIdx)
        {
            for(uint16_t pixelIdx = 0; pixelIdx < WALL_SIDE_LENGTH; ++pixelIdx)
            {
                HSV outColor;

                int x = sideColumnIdx % 2 == 0 ? pixelIdx : WALL_SIDE_LENGTH - pixelIdx;
                int y = (sideIdx * 2) + sideColumnIdx;
                int stripPixelIdx = (y*WALL_SIDE_LENGTH) + pixelIdx;

                float noiseAlpha = coreNoise.evaluate(x, y);
                ecore::HSVPalette& Palette = palettes[sideIdx];
                outColor = Palette.getColor(noiseAlpha);

                //GM.OutfitLEDs->setHSV(stripPixelIdx, outColor);
            }
        }
    }

    //GM.OutfitLEDs->updateStripPixels();
}

State_Obelisk_Theater::State_Obelisk_Theater(const char* InStateName) : State(InStateName)
{
    lfo.width = 24.f;
    lfo.speed = -1.5f;

    paletteLFO.speed = 0.1f;


    coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    coreNoise.noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Hybrid);
    coreNoise.noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
    coreNoise.noise.SetFrequency(0.05f);
    coreNoise.noise.SetCellularJitter(1.0f);
    coreNoise.timeScale = 0.33f;
    coreNoise.imageScaleX = 1.0f;
    coreNoise.imageScaleY = 1.0f;
}

void State_Obelisk_Theater::onStateBegin()
{
    State::onStateBegin();
}

void State_Obelisk_Theater::tick(float deltaTime)
{
    State::tick(deltaTime);

    //GameManager& GM = GameManager::get();

    lfo.tick(deltaTime);
    paletteLFO.tick(deltaTime);
    coreNoise.tick(deltaTime);

    ecore::HSVPalette palette = jpalettes::p_darkpurple_neo;

    for(uint16_t pixelIdx = 0; pixelIdx < 210; ++pixelIdx)
    {
        HSV outColor;

        float noiseAlpha = lfo.evaluate(pixelIdx);
        
        /*
        for(uint8_t paletteIdx = 0; paletteIdx < palettes.size(); ++paletteIdx)
        {
            palette.colors.push_back(palettes[paletteIdx].getColor(noiseAlpha));
        }
        */
        outColor = palette.getColor(noiseAlpha);

        //GM.OutfitLEDs->setHSV(pixelIdx, outColor);
    }

    //GM.OutfitLEDs->updateStripPixels();
}