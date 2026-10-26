// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

namespace ecore
{
    class PropertyBag;

    /* @brief Something with knobs, clocks or shapes a desk can reach by name.
    *
    * One method. Implement it to hand out what is worth touching from
    * outside - a knob to turn, a clock to match, a curve to redraw - one
    * line per property, into the bag:
    *
    *     void MyLook::reflect(ecore::PropertyBag& bag)
    *     {
    *         bag.add("gain", gain, 0.0f, 2.0f);
    *         bag.addState("time", timeActive);
    *         bag.reflect("noise", noise);        // a child's properties, as noise.*
    *     }
    *
    * A base class rather than anything cleverer because the library builds
    * without RTTI on the microcontroller side, so there is nothing to cast
    * to. Every GeneratorHSV is one, saying nothing until it has knobs; an
    * LFO or a noise field is one on its own, since it has clocks to match
    * without rendering anything. @see ecore::PropertyBag
    */
    class Reflectable
    {
    public:
        virtual ~Reflectable() = default;

        virtual void reflect(PropertyBag& bag) = 0;
    };
}
