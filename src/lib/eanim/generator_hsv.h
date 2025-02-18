// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

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

    * frankly, partially going toward deprecation, use GeneratorHSV_Cont and GeneratorHSV_Cont2D if possible
    */
    class GeneratorHSV
    {
    public:
        // generate colozr for provided index
        virtual void applyEffectLogic(uint16_t idx, HSV& InOutColor) const = 0;
    };

    /// Generator HSV Continious
    /// 
    /// should support blending between positions
    /// 
    /// should assume x scale is relative to the led strip's index, and thus the led distance
    class GeneratorHSV_Cont : public GeneratorHSV
    {
    public:
        // continious can blend between indexes if desired
        virtual void applyEffectLogic(float x, HSV& InOutColor) const = 0;
        
        virtual void applyEffectLogic(uint16_t idx, HSV& InOutColor) const override;
    };

    /// Generator HSV Continious 2D
    ///
    /// should assume x scale is relative to the led strip's index, and thus the led distance
    class GeneratorHSV_Cont2D : public GeneratorHSV_Cont
    {
    public:
        virtual void applyEffectLogic(float x, float y, HSV& InOutColor) const = 0;

        virtual void applyEffectLogic(float x, HSV& InOutColor) const override;
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