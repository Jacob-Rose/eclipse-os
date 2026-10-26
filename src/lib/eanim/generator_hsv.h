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
    /* @brief The base of every look: a colour for a node, a clock, and a
    * name for each of its knobs.
    *
    * render() is asked once per node per frame with a colour in hand -
    * the node's last frame by default, black if the state running the
    * look asks for it (State_GenericHSV::RenderBase), the layer below's
    * output in a LayeredGeneratorHSV. A look paints over it, blends into
    * it, or leaves it alone. tick() advances whatever the look keeps
    * between frames. reflect() hands out the look's properties, and says
    * nothing by default; see ecore::Reflectable.
    */
    class GeneratorHSV : public Tickable, public ecore::Reflectable
    {
    public:
        // generate color for provided index
        // node is provided for any required context
        virtual void render(HSVStripNode* node, HSV& InOutColor) const = 0;
        virtual void tick(float /*deltaTime*/) {} // optional, if the generator needs to update any internal state

        // Reflectable interface: nothing, until a look has knobs
        virtual void reflect(ecore::PropertyBag& /*bag*/) override {}

        /* @brief Something happened, now - a sound played, a tag landed.
        *
        * An impulse, by tag. The desk sends `trigger <tag>` at the moment the
        * game plays a sound, and a look that answers to that tag does its thing off
        * it - restarts an envelope, re-anchors a clock - so the light is
        * *tied* to the sound rather than running beside it on a clock of
        * its own that happens to agree. False, the default, means this look
        * has no such trigger, which the desk reports rather than swallows.
        *
        * The tag is a GameplayTag - dotted text compared by hash - and the
        * ones a look answers to are declared once beside it, not spelled at
        * the compare site:
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
    * The one way looks combine. Each layer gets the colour the layer below
    * left for the node and paints over it, blends into it, or leaves it;
    * the bottom layer gets whatever the stack was handed. So a look never
    * knows or cares whether it is stacked - a desk composing a sculpture's
    * own look under a scanner's is two layers, and so is any other stack.
    * Ticks every layer. Owns nothing it was not given.
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
