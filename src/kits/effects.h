// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <list>

#include "../lib/ecore/core.h"
#include "../lib/ecore/range.h"

#include "../lib/eanim/generator_float.h"
#include "../lib/eanim/generator_hsv.h"

using namespace ecore;

namespace eanim
{

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
        virtual float evaluate(float x) const override;

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
    */
    class DropGenerator : public GeneratorHSV
    {
    public:
        DropGenerator(uint16_t inLength);

        HSVPalette dropPalette;
        FloatRange dropRate = FloatRange(0.1f, 1.0f);
        

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // GeneratorHSV interface
        virtual void render(HSVStripNode* node, HSV& InOutColor) const override;

    private:
        struct Drop
        {
            int idx;
            float size;
            HSV color;
            float speed;
        };

        std::list<std::shared_ptr<Drop>> drops;
        float timeSinceLastDrop = 0.0f;

        std::vector<HSV> renderBuffer;


        int length;
    };


    // TODO: Implement this
    /* @brief A generator that will generate a laser scan effect like night rider */
    class LaserScanGenerator : public Generator1D
    {
        std::shared_ptr<FloatAttribute> position;
        std::shared_ptr<FloatAttribute> falloffDistance;
    };
}