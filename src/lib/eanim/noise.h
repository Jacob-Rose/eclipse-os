// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>

#include "../ecore/property.h"

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


    /* @brief A 2d noise field that drifts with time.
    *
    * Configure `noise` (FastNoiseLite) directly for type and frequency; scale
    * the sample space with imageScaleX/Y and the clock with timeScale.
    * evaluate(x, y) samples it.
    */
    class PerlinNoiseGenerator2D : public Generator2D, public Tickable, public ecore::Reflectable
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

        /// Reflectable interface: the state that makes this field this field,
        /// `seed` and `time`, so a copy elsewhere can be put at the same picture.
        virtual void reflect(ecore::PropertyBag& bag) override;

        void setSeed(int inSeed);
        int getSeed() const { return seed; }
        float getCurrentTime() const { return currentTime; }

        /// The clock loops every kLoopLength clock units (seconds * timeScale)
        /// so it never loses float precision; the last kLoopFade of each loop
        /// cross-fades into the next, which costs a second sample per pixel.
        static constexpr float kLoopLength = 1024.0f;
        static constexpr float kLoopFade = 8.0f;

    private:
        float currentTime;
        int seed{0};
        /// the seed as the property bag sees it, applied back through setSeed
        float seedState{0.0f};
    };
}
