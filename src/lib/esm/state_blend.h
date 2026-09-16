// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../ecore/hsv.h"

namespace esm
{
    /// How a rig gets from one look to the next.
    ///
    /// A transition is two looks rendering into their own buffers and one of
    /// these deciding what each node shows in between. The machine owns the
    /// clock and the states; a blend is only ever asked "this far through the
    /// change, given the colour leaving and the colour arriving, what is this
    /// node?" - so it is a pure function with no per-node state, and one
    /// instance serves every machine on the device. That is what makes them
    /// interchangeable: a cue names one, the machine points at it, nothing is
    /// rebuilt.
    class StateBlend
    {
    public:
        virtual ~StateBlend() = default;

        virtual const char* getName() const = 0;

        /// `alpha` runs 0 -> 1 over the transition. `position` is the node's
        /// place along its strip, 0 -> 1, for a blend that travels across the
        /// rig rather than happening everywhere at once.
        virtual ecore::HSV mix(const ecore::HSV& from, const ecore::HSV& to,
                               float alpha, float position) const = 0;

        /// A blend with nothing to show between the two looks. The machine
        /// skips the transition outright rather than running one whose every
        /// frame is the destination.
        virtual bool isInstant() const { return false; }
    };

    /// The built-in blend by that name, or null. Names are the ones a cue
    /// line and a config spell: `cut`, `crossfade`, `rgb`, `dip`, `wipe`.
    const StateBlend* findStateBlend(const char* name);

    /// `crossfade` - what every machine did before blends had names, so a
    /// machine that never chooses looks exactly as it always has.
    const StateBlend& defaultStateBlend();

    /// Every built-in name, in a fixed order, ending in null. For a UI's list
    /// and an error message's "have: ...".
    const char* const* stateBlendNames();
}
