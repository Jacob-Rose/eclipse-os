// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include "janim.h"
#include "jgenerator.h"

#include <list>

namespace j
{
    /* @brief A fire generator, moving in one direction. We making candles
    * moves in one direction from a source
    * 
    * Currently In-Development, not working
    */
    class FireEmitter : public Generator1D, public Tickable
    {
    public:
        FireEmitter(uint16_t inLength);

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // Generator1D interface
        virtual float evaluate(float val) const override;


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
    */ 
    class DropEmitter : public GeneratorHSV
    {
        struct Drop
        {
            int idx;
            float size;
        };

        std::list<Drop> drops;
    };
}