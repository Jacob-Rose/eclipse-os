// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>
#include <vector>
#include <memory>

#include "../ecore/core.h"
#include "../ecore/hsv.h"
#include "../ecore/property.h"
#include "../ecore/tickable.h"

#include "../eio/strip_projection.h"

using namespace ecore;
using namespace eio;


namespace eanim
{

    /* @brief A Generator that can provide or process colors
    * these can also handle alphas and blending between layers, but that is not required

    * frankly, partially going toward deprecation, use GeneratorHSV_Cont and GeneratorHSV_Cont2D if possible
    */
    class GeneratorHSV : public Tickable
    {
    public:
        // generate color for provided index
        // node is provided for any required context
        virtual void render(HSVStripNode* node, HSV& InOutColor) const = 0;
        virtual void tick(float /*deltaTime*/) {} // optional, if the generator needs to update any internal state

        /* @brief Hand out the knobs worth turning while this look runs.
        *
        * Optional, and empty by default: a look that has nothing to tune says
        * nothing. One line per property, and only the ones that actually change
        * how the look reads - this is a tuning surface, not a dump of every
        * member.
        *
        *     void MyLook::reflect(ecore::PropertyBag& bag)
        *     {
        *         bag.add("gain", gain, 0.0f, 2.0f);
        *     }
        *
        * A virtual rather than anything cleverer because the library builds
        * without RTTI on the microcontroller side, so there is nothing to cast
        * to. @see ecore::PropertyBag
        */
        virtual void reflect(ecore::PropertyBag& /*bag*/) {}
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