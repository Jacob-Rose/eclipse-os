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

        /// What makes this field this field: its seed, drawn at random when
        /// it was built, and its clock - as `<prefix>seed` and `<prefix>time`,
        /// so a copy of the look elsewhere can be put at the same picture.
        /// The rest (frequency, type, scale) is the look's own constants and
        /// travels in its code. @see eanim::GeneratorHSV::reflectState
        void reflectState(ecore::PropertyBag& bag, const std::string& prefix);

        void setSeed(int inSeed);
        int getSeed() const { return seed; }
        float getCurrentTime() const { return currentTime; }

    private:
        float currentTime;
        int seed{0};
        /// the seed as the bag sees it: a float, applied back through
        /// setSeed when a state lands. rand() fits a float exactly.
        float seedState{0.0f};
    };
}