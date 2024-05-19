// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include "janim.h"

#include <list>

namespace j
{
    class FireEmitter
    {
    public:
        void init(uint8_t inSize);

        float cooldown = 0.1f; // fade per second
        float spreadRate = 1.0f;
        Saw flameSourceSaw = Saw(6.0f);
        std::vector<float> flameSourceAnim = {0.0f, 1.0f};//{0.0f,0.5f,0.2f, 0.7f,0.9f,0.3f,0.6f, 1.0f, 0.3f,0.5f,0.0f};
        float flameSourcePower = 10.0f;
        bool bReverse = false;

        void tick(float deltaTime);
        //todo make generator
        virtual float evaluate(int idx) const;

    protected:
        uint8_t size;
        std::vector<float> heatValues;
    };


    class DropEmitter
    {
        struct Drop
        {
            int idx;
            float size;
        };

        std::list<Drop> drops;
    };
}