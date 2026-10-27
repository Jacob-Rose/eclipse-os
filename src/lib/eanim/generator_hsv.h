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
#include "../ecore/name.h"
#include "../ecore/reflectable.h"
#include "../ecore/tickable.h"

#include "../eio/strip_projection.h"

using namespace ecore;
using namespace eio;


namespace eanim
{
    /* @brief The base of every look: a colour per node, a clock, and its knobs.
    *
    * render() is called once per node per frame with a colour in hand: the
    * node's last frame by default, black if the state running the look asks
    * for it (State_GenericHSV::RenderBase), or the layer below's output in a
    * LayeredGeneratorHSV. Paint over it, blend into it, or leave it alone.
    * tick() advances whatever the look keeps between frames. reflect() hands
    * out the look's properties and says nothing by default.
    *
    * @see ecore::Reflectable
    * @see LayeredGeneratorHSV
    */
    class GeneratorHSV : public Tickable, public ecore::Reflectable
    {
    public:
        /// The colour for `node`, given what is already there.
        virtual void render(HSVStripNode* node, HSV& InOutColor) const = 0;
        virtual void tick(float /*deltaTime*/) {}

        // Reflectable interface: nothing, until a look has knobs
        virtual void reflect(ecore::PropertyBag& /*bag*/) override {}

        /* @brief An impulse by tag: `trigger <tag>` from the desk when the game
        * plays a sound. Return true if this look answered to it; false, the
        * default, means it has no such trigger.
        *
        * Declare the tags a look answers to once, beside it:
        *
        *     namespace my_tags { inline const GameplayTag Hit{"my.hit"}; }
        *
        *     bool MyLook::onTrigger(const GameplayTag& tag)
        *     {
        *         if (tag != my_tags::Hit) return false;
        *         envelope.trigger();
        *         return true;
        *     }
        */
        virtual bool onTrigger(const GameplayTag& /*tag*/) { return false; }
    };


    /* @brief A stack of looks rendered in order, bottom first.
    *
    * Each layer gets the colour the layer below left for the node; the bottom
    * layer gets whatever the stack was handed. A look never knows whether it
    * is stacked. Ticks every layer. Does not own what it is given.
    */
    class LayeredGeneratorHSV : public GeneratorHSV
    {
    public:
        void addLayer(std::shared_ptr<GeneratorHSV> layer)
        {
            if (layer)
            {
                layers.push_back(std::move(layer));
            }
        }

        void clearLayers() { layers.clear(); }
        const std::vector<std::shared_ptr<GeneratorHSV>>& getLayers() const { return layers; }

        virtual void render(HSVStripNode* node, HSV& inOutColor) const override
        {
            for (const std::shared_ptr<GeneratorHSV>& layer : layers)
            {
                layer->render(node, inOutColor);
            }
        }

        virtual void tick(float deltaTime) override
        {
            for (const std::shared_ptr<GeneratorHSV>& layer : layers)
            {
                layer->tick(deltaTime);
            }
        }

    private:
        std::vector<std::shared_ptr<GeneratorHSV>> layers;
    };
}
