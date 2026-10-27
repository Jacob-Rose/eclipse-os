// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/hsv.h>

namespace esm
{
    /* @brief How a rig gets from one look to the next.
    *
    * During a transition two states render into their own buffers and the
    * machine asks the blend, per node, what to show between them. A blend is
    * a pure function with no per-node state, so one instance serves every
    * machine. Pick one by name with StateMachine::setBlend().
    */
    class StateBlend
    {
    public:
        virtual ~StateBlend() = default;

        virtual const char* getName() const = 0;

        /// `alpha` runs 0 -> 1 over the transition. `position` is the node's
        /// place along its strip, 0 -> 1, for a blend that travels the rig.
        virtual ecore::HSV mix(const ecore::HSV& from, const ecore::HSV& to,
                               float alpha, float position) const = 0;

        /// True for a blend with nothing to show in between; the machine
        /// then lands the next state at once.
        virtual bool isInstant() const { return false; }
    };

    /// The built-in blend by that name, or null: `cut`, `crossfade`, `rgb`,
    /// `dip`, `wipe`.
    const StateBlend* findStateBlend(const char* name);

    /// `crossfade`, the straight HSV lerp every machine starts with.
    const StateBlend& defaultStateBlend();

    /// Every built-in name in a fixed order, ending in null.
    const char* const* stateBlendNames();
}
