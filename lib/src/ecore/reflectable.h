// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

namespace ecore
{
    class PropertyBag;

    /* @brief Something with properties a desk can reach by name.
    *
    * Implement reflect() to hand out what is worth touching from outside,
    * one line per property:
    *
    *     void MyLook::reflect(ecore::PropertyBag& bag)
    *     {
    *         bag.add("gain", gain, 0.0f, 2.0f);   // a knob with a slider range
    *         bag.addState("time", timeActive);    // a clock, for the sim handshake
    *         bag.add("envelope", envelope.curve); // a shape for a curve editor
    *         bag.reflect("noise", noise);         // a child's properties, as noise.*
    *     }
    *
    * Every GeneratorHSV is one and says nothing by default; an LFO or a noise
    * field is one on its own. There is no RTTI on the microcontroller, so an
    * object is reached through this base, never a cast. @see ecore::PropertyBag
    */
    class Reflectable
    {
    public:
        virtual ~Reflectable() = default;

        virtual void reflect(PropertyBag& bag) = 0;
    };
}
