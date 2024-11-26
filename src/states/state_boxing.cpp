// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_boxing.h"

#include "../lib/ecore/math.h"
#include "../lib/ecore/logging.h"
#include "../gm.h"
#include "../kits/palettes.h"

#include "../imgs/eclipse.h"

using namespace eanim;

State_Boxing_Noise::State_Boxing_Noise(const char* InStateName) : State(InStateName)
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

void State_Boxing_Noise::onStateBegin()
{
    State::onStateBegin();
}

void State_Boxing_Noise::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    float deltaTime = lastFrameDT.count();

    coreNoise.tick(deltaTime);
    for(int testCount = 0; testCount < 20; testCount++)
    {
        static constexpr uint16_t pixelCount = LED_COUNT;
        for(uint16_t pixelIdx = 0; pixelIdx < pixelCount; ++pixelIdx)
        {
            HSV outColor;

            int x = pixelIdx / COLUMN_LENGTH;
            int y = pixelIdx % COLUMN_LENGTH;
            if(x % 2 == 0)
            {
                y = (COLUMN_LENGTH-1) - y;
            }

            float noiseAlpha = coreNoise.evaluate(x, y);
            outColor = palette.getColor(noiseAlpha);

            GM.OutfitLEDs->setHSV(pixelIdx, outColor);
        }
    }

    GM.OutfitLEDs->updateStripPixels();
}

State_Boxing_Theater::State_Boxing_Theater(const char* InStateName) : State(InStateName)
{
    lfo.width = 24.f;
    lfo.speed = -1.5f;

    paletteLFO.speed = 0.1f;
}

void State_Boxing_Theater::onStateBegin()
{
    State::onStateBegin();
}

void State_Boxing_Theater::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    float deltaTime = lastFrameDT.count();

    lfo.tick(deltaTime);
    paletteLFO.tick(deltaTime);

    for(uint16_t pixelIdx = 0; pixelIdx < LED_COUNT; ++pixelIdx)
    {
        HSV outColor;

        int x = pixelIdx / COLUMN_LENGTH;
        int y = pixelIdx % COLUMN_LENGTH;
        if(x % 2 == 0)
        {
            y = (COLUMN_LENGTH-1) - y;
        }

        float noiseAlpha = lfo.evaluate(x + y);
        outColor = palette.getColor(noiseAlpha);

        GM.OutfitLEDs->setHSV(pixelIdx, outColor);
    }

    delay(15);

    GM.OutfitLEDs->updateStripPixels();
}


State_Boxing_Iterate::State_Boxing_Iterate(const char* InStateName) : State(InStateName)
{

}

void State_Boxing_Iterate::onStateBegin()
{
    State::onStateBegin();
}

void State_Boxing_Iterate::tick()
{
    State::tick();

    GameManager& GM = GameManager::get();

    float deltaTime = lastFrameDT.count();
    trackedDeltaTime += deltaTime;

    while(trackedDeltaTime > timescale)
    {
        idx++;
        idx = idx % LED_COUNT;
        trackedDeltaTime -= timescale;
    }

    for(uint16_t pixelIdx = 0; pixelIdx < LED_COUNT; ++pixelIdx)
    {
        HSV outColor;
        outColor = palette.getColor(pixelIdx == idx);

        GM.OutfitLEDs->setHSV(pixelIdx, outColor);
    }

    GM.OutfitLEDs->updateStripPixels();
}