// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "generic_patterns.h"

///
/// The scanner's looks that are built on a generic one - the recording
/// flow's, and the secret playback's rain.
///
/// scanner_patterns.h holds the looks written for the game; generic_patterns.h
/// the free-standing stage looks that belong to no state. A game look that
/// *is* a generic look with a game's demands on it - a cue to answer, a
/// picture to sit over - needs both complete, so it lives here, after them.
///

namespace scanner_tags
{
    /// a rock has landed on an armed recording and its take is about to
    /// roll: record_arm runs its fire's emitter down, and the game waits
    /// that long before it fades - see Pattern_Scanner_ArmFire::douseSeconds
    /// and RecordArmState in afterglow's game.py
    inline const GameplayTag Douse{"scanner.arm.douse"};
}

namespace scanner
{
    /* @brief record_arm: the fire burns while the rig waits for the rock
    * that will hold the take, over the sculpture's own picture.
    *
    * Fire2012 on the stage - the ember bed along the foot, risers climbing
    * through the ring and on up the runs - blended over the underlay (the
    * obelisk's own look, shadowed on the desk; see eanim::Underlay) by its
    * own heat. Where no flame is, the tower is the sculpture's own, and the
    * flames lick over it, the way scan_idle's scan line claims the tower
    * only where it is. Without an underlay it is the fire over black, which
    * is what it is on the ring.
    *
    * The game douses it as the rock lands: `trigger scanner.arm.douse`
    * runs the emitter down to nothing over douseSeconds, and the game holds
    * the state that long before fading to record_countdown - so the flames it
    * hands over are already dying, not cut off mid-burn. Entry relights it.
    */
    class Pattern_Scanner_ArmFire : public Pattern_Generic_Fire2012
    {
    public:
        /// how long the douse takes the emitter from full to nothing. The
        /// game's ARM_DOUSE_SECONDS is this: change one, change the other.
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

    /* @brief audio_playback_generic: the secret playback, an easter egg's
    * rain - the Mythos set's rainbow rain in the playback's own colours.
    *
    * The matrix rain, every drop its own hue, but the hues are the old
    * ramp's: orange through yellow to green (baseHue 30, hue_spread a
    * quarter of the wheel), falling over the whole stage while the item's
    * clip plays. Opens already raining - the storm is rolled forward on
    * entry, as the Mythos rain's is - so the playback does not start on an
    * empty sky.
    */
    class Pattern_Scanner_SecretRain : public Pattern_Generic_MatrixRain
    {
    public:
        Pattern_Scanner_SecretRain();

        virtual void reset() override;
    };

    /* @brief secret_alien: the alien rock's message - a saucer over the
    * tower with its tractor beam down it.
    *
    * The ring is the saucer: `lights` running lights orbiting it, green and
    * magenta by turns, over a dim teal hull. The rest of the stage is the
    * beam: bands of pale green falling from the tip to the foot at
    * `fall_rate` bands a second, `bands` of them up the height, over a faint
    * glow - and the whole beam stutters now and then, as a transmission
    * from that far off would. From the scan straight through the message,
    * no YES between: afterglow's item data names it (`look`).
    */
    class Pattern_Scanner_SecretAlien : public PatternScanner
    {
    public:
        HSV beamColor = HSV(110.0f, 0.55f, 1.0f);
        HSV hullColor = HSV(175.0f, 0.9f, 0.18f);
        HSV lightA = HSV(100.0f, 1.0f, 1.0f);
        HSV lightB = HSV(300.0f, 0.9f, 1.0f);
        float lights = 6.0f;
        float orbitRate = 0.45f;     // turns a second
        float bands = 4.0f;          // up the stage's height
        float fallRate = 0.8f;       // bands a second, tip to foot
        float glow = 0.3f;           // the beam between its bands
        float stutter = 0.35f;       // how deep the transmission drops out

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };

    /* @brief secret_frog: the frog's voice - a fantasy forest at night.
    *
    * A canopy of deep greens drifting slowly over the stage, moss into
    * pine, with fireflies: each pixel its own, winking warm yellow-green on
    * its own slow clock so a few are always lit and none together. A light
    * shaft wanders across now and then, lifting the canopy where it falls.
    * From the scan straight through the voice, no YES between: afterglow's
    * item data names it (`look`).
    */
    class Pattern_Scanner_SecretForest : public PatternScanner
    {
    public:
        HSV mossColor = HSV(95.0f, 0.85f, 0.45f);
        HSV pineColor = HSV(145.0f, 0.95f, 0.16f);
        HSV fireflyColor = HSV(70.0f, 0.85f, 1.0f);
        float drift = 0.25f;         // how fast the canopy moves
        float fireflyRate = 0.6f;    // winks a second, each firefly
        float fireflies = 14.0f;     // sharpness: higher is fewer, briefer
        float shaft = 0.35f;         // how much a light shaft lifts the canopy

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };
}
