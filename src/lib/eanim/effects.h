// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <list>

#include "../external/FastNoiseLite.h"

#include "generator_float.h"
#include "generator_hsv.h"

using namespace ecore;

namespace eanim
{
    //TODO lots of noise functions https://gametorrahod.com/various-noise-functions/


    /* @brief Wrapper for FastNoiseLite to make it support float attributes
    */
    class PerlinNoiseGenerator1D : public Generator1D, public Tickable
    {
    public:
        PerlinNoiseGenerator1D();

        void init();

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // Generator1D interface
        virtual float evaluate(float value) const override;

        float imageScale = 1.0f;
        float timeScale = 1.0f;

        FastNoiseLite noise;
    private:
        float currentTime;
    };

    /* @brief A fire generator, moving in one direction. We making candles
    * moves in one direction from a source
    * 
    * Currently In-Development, not working
    */
    class FireGenerator : public Generator1D, public Tickable
    {
    public:
        FireGenerator(uint16_t inLength);

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // Generator1D interface
        virtual float evaluate(float value) const override;

        HSVPalette palette;
        float cooldown = 0.1f; // fade per second
        float spreadRate = 1.0f; 
        Saw flameSourceSaw = Saw(6.0f);
        std::vector<float> flameSourceAnim = {0.0f, 1.0f};//{0.0f,0.5f,0.2f, 0.7f,0.9f,0.3f,0.6f, 1.0f, 0.3f,0.5f,0.0f};
        float flameSourcePower = 10.0f;
        bool bReverse = false;
    protected:
        std::vector<float> heatValues;
    };


    /* @brief A generator that will generate particles of various sizes 
    *
    * TODO: Copy logic from drop state machine
    * 
    * Currently In-Development, not working
    */ 
    class DropGenerator : public GeneratorHSV, public Tickable
    {
    public:
        DropGenerator(uint16_t inLength);

        HSVPalette dropPalette;
        

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // GeneratorHSV interface
        virtual void applyEffectLogic(std::vector<HSV>& InOutColors) const override;

    private:
        struct Drop
        {
            int idx;
            float size;
        };

        std::list<Drop> drops;

        int length;
    };


    class LaserScanGenerator : public Generator1D, public Tickable
    {
        std::shared_ptr<FloatAttribute> position;
        std::shared_ptr<FloatAttribute> falloffDistance;
    };
}