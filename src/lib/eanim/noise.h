// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <list>

#include "../external/FastNoiseLite.h"

#include "../ecore/core.h"
#include "../ecore/range.h"

#include "generator_float.h"
#include "generator_hsv.h"

using namespace ecore;

namespace eanim
{
    //TODO lots of noise functions https://gametorrahod.com/various-noise-functions/


    /* @brief Wrapper for FastNoiseLite to make it support float attributes
    */
    class PerlinNoiseGenerator2D : public Generator2D, public Tickable
    {
    public:
        PerlinNoiseGenerator2D();

        void init();

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // Generator2D interface
        virtual float evaluate(float x, float y) const override;

        float imageScaleX = 1.0f;
        float imageScaleY = 1.0f;
        float timeScale = 1.0f;

        FastNoiseLite noise;
    private:
        float currentTime;
    };
}