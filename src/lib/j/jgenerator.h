// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"

#include <Arduino.h>

#include <string>
#include <vector>
#include <memory>

#include "jcolors.h"
#include "jtickable.h"


namespace j
{

    /*
    * Composite Generator supports a stack of effects
    */
    class GeneratorHSV
    {
    public:
        GeneratorHSV(uint16_t inLength);
    
        virtual uint16_t getLength() const = 0;
        virtual void applyEffectLogic(std::vector<HSV> InOutColors) = 0;

    protected:
        uint16_t length; 
    };


    /*
    * Composite Generator supports a stack of effects
    */
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
}