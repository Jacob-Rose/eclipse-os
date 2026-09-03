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
#include "../ecore/gameplay_tag.h"
#include "../ecore/property.h"
#include "../ecore/tickable.h"

#include "../eio/strip_projection.h"

using namespace ecore;
using namespace eio;


namespace eanim
{
    /* @brief What is showing *underneath* a look, on an object something else
    * is also lighting.
    *
    * The case it exists for: a desk has taken a sculpture's pixels over its
    * cable, and the sculpture's own look is still running - paused on the
    * sculpture, simulated in step on the desk (see edmx::RelicShadow). A look
    * that wants to *join* that picture rather than paint over it samples the
    * underlay for a node and blends by how much of the node it claims. Null
    * everywhere the question does not arise, which is every relic and every
    * desk with nothing linked - so a look treats it as optional and renders
    * exactly as before without one.
    */
    class Underlay
    {
    public:
        virtual ~Underlay() = default;

        /// The colour under `node`, or false when the underlay has nothing
        /// for it (a node on an object the underlay is not simulating).
        virtual bool sample(const HSVStripNode* node, HSV& outColor) const = 0;
    };


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

        /* @brief Hand out the shapes worth drawing while this look runs.
        *
        * reflect(), for AutomationCurves: an envelope or a swell a desk's
        * curve editor can load live and write an edited shape back into.
        * Same rules - optional, empty by default, one line per curve, and
        * only the shapes that actually change how the look reads.
        * @see eanim::CurveBag
        */
        virtual void reflectCurves(class CurveBag& /*bag*/) {}

        /* @brief Hand out the clocks this look is running on.
        *
        * The fourth surface, and the one that lets a look run in two places
        * at once. reflect() is the knobs; this is the *state* - a noise
        * field's time, an LFO's phase, the seconds since a cue - registered
        * with PropertyBag::addState so the same bag can be read out as
        * `name=value` text (ecore::serializeState) and written back
        * (ecore::applyState). A relic answers a desk's `sim` with it; the
        * desk spawns the same look, applies the answer, and from then on
        * has the sculpture's picture without a pixel crossing the cable.
        *
        * Optional, empty by default: a look with no clocks (a solid colour)
        * has nothing to say and needs nothing to match. A look that has
        * clocks and says nothing simply cannot be simulated - the desk
        * falls back to blending from black.
        *
        *     void MyLook::reflectState(ecore::PropertyBag& bag)
        *     {
        *         noise.reflectState(bag, "noise.");
        *         bag.addState("time", timeActive);
        *     }
        */
        virtual void reflectState(ecore::PropertyBag& /*bag*/) {}

        /* @brief The underlay, for a look that composes over one. Set by the
        * desk while a relic is linked; null otherwise. @see eanim::Underlay */
        void setUnderlay(const Underlay* inUnderlay) { underlay = inUnderlay; }
        const Underlay* getUnderlay() const { return underlay; }

        /* @brief Whether this look leaves `node` to the underlay entirely.
        *
        * The renderer samples the underlay in place of calling render() for
        * such a node, so a look that has nothing to say about an object - a
        * boot animation on the ring, a dark idle - shows what is underneath
        * it rather than painting it black. It is what makes a takeover, or
        * a cross-fade into such a look, invisible on the sculpture. Only
        * consulted while an underlay is set; false by default, which is
        * "this look paints everything it is given".
        */
        virtual bool leavesToUnderlay(const HSVStripNode* /*node*/) const { return false; }

    protected:
        const Underlay* underlay{nullptr};

    public:

        /* @brief Something happened, now - a sound played, a tag landed.
        *
        * The third surface after the knobs and the shapes: an impulse, by
        * tag. The desk sends `trigger <tag>` at the moment the game plays
        * a sound, and a look that answers to that tag does its thing off
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