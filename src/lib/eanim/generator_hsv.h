// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <string>
#include <vector>
#include <memory>

#include "../ecore/hsv.h"
#include "../ecore/tickable.h"

using namespace ecore;


namespace eanim
{

    /* @brief A Generator that can provide or process colors
    * due to the nature of this setup, these can also handle alphas and blending between layers 
    */
    class GeneratorHSV
    {
    public:
        // a list of HSV colors. generators should support any length provided
        virtual void applyEffectLogic(std::vector<HSV>& InOutColors) const;
        virtual void applyEffectLogic(uint16_t idx, HSV& InOutColor) const;
    };


    /*
    * Composite Generator supports a stack of effects
    * unproven and untested, assumed overkill but leaving here for possible change in future
    */
   /*
    class CompositeGeneratorHSV : public GeneratorHSV, public Tickable
    {
    public:
        CompositeGeneratorHSV(uint16_t inLength);

        virtual void tick(float deltaTime) override;

        virtual HSV getPixelColor(int ledIdx);

    private:
        struct GeneratorLayerInfo
        {
            int startOffset;
            std::unique_ptr<GeneratorHSV> generator;
        };

        std::vector<GeneratorLayerInfo> generatorLayers;

        std::vector<HSV> pixelData;
     };


    class PatternHSV
    {
    public:
        PatternHSV(uint16_t inLength);

        uint16_t getLength() { return length; }
        j::HSV getPixelColor(int ledIdx);

    private:
        std::unique_ptr<CompositeGeneratorHSV> rootLayer;

        uint16_t length;
    };
    */
}