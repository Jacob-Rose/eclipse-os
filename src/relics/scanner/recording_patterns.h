// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "generic_patterns.h"

///
/// The recording flow's looks that are built on a generic one.
///
/// scanner_patterns.h holds the looks written for the game; generic_patterns.h
/// the free-standing stage looks that belong to no state. A game look that
/// *is* a generic look with a game's demands on it - a cue to answer, a
/// picture to sit over - needs both complete, so it lives here, after them.
///

namespace scanner_tags
{
    /// the rock has landed on an armed cleanse and its take is about to go:
    /// cleanse_arm runs its fire's emitter down, and the game waits that
    /// long before it fades - see Pattern_Scanner_CleanseFire::douseSeconds
    /// and CleanseArmState in afterglow's game.py
    inline const GameplayTag Douse{"scanner.cleanse.douse"};
}

namespace scanner
{
    /* @brief cleanse_arm: the fire burns while the rig waits for the rock
    * whose take is to be cleansed, over the sculpture's own picture.
    *
    * Fire2012 on the stage - the ember bed along the foot, risers climbing
    * through the ring and on up the runs - blended over the underlay (the
    * obelisk's own look, shadowed on the desk; see eanim::Underlay) by its
    * own heat. Where no flame is, the tower is the sculpture's own, and the
    * flames lick over it, the way scan_idle's scan line claims the tower
    * only where it is. Without an underlay it is the fire over black, which
    * is what it is on the ring.
    *
    * The game douses it as the rock lands: `trigger scanner.cleanse.douse`
    * runs the emitter down to nothing over douseSeconds, and the game holds
    * the state that long before fading to cleanse_done - so the flames it
    * hands over are already dying, not cut off mid-burn. Entry relights it.
    */
    class Pattern_Scanner_CleanseFire : public Pattern_Generic_Fire2012
    {
    public:
        /// how long the douse takes the emitter from full to nothing. The
        /// game's CLEANSE_DOUSE_SECONDS is this: change one, change the other.
        float douseSeconds = 0.3f;

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// scanner_tags::Douse: the rock landed - let the fire die
        virtual bool onTrigger(const GameplayTag& tag) override;

    private:
        bool doused{false};
        float sinceDouse{0.0f};
    };
}
