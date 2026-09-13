// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../lib/eanim/particles.h"

#include "scanner_patterns.h"

///
/// The generic looks: stage patterns that belong to no game state.
///
/// Everything here renders against the scanner's stage - the ring below, the
/// obelisk above, continuous x and y - but none of it is afterglow's: these
/// are the free-standing looks (several recreated from WLED) that any rig on
/// the stage can wear. They live beside the scanner patterns because the
/// stage constants do, and they share PatternScanner's resettable clock.
///
/// The house rules they all follow: a look is a continuous field over (x, y),
/// never a per-strip index walk, so it reads correctly on the ring's arc and
/// on the obelisk's runs alike; randomness that has to hold still frame to
/// frame is ecore::hash01, never get_random_float.
///

namespace scanner
{
    /* @brief Two-lattice value noise to 0..1, smooth in both axes - hash01 on
    * the integer corners, smoothstepped bilinear between them.
    *
    * The texture under the waterfall and the colour clouds, and shared so a
    * look elsewhere that wants a drifting field gets the same one rather
    * than a second lattice that agrees with this one by accident.
    */
    float valueNoise(float x, float y);


    /* @brief Matrix rain: green code falling down the stage.
    *
    * The drops are particles - real positions in the plane with a radius,
    * integrated in tick() through eanim::ParticleSystem - so a drop's head
    * is a soft radial glow that slides smoothly through whatever geometry
    * it passes: on and between the obelisk's runs, and *through* the ring's
    * circle, brightening the arc's pixels exactly as it crosses them. A
    * drop that falls off the bottom respawns at the top at a fresh x, so
    * the rain never settles into fixed paths. The tail is a streak hanging
    * up the particle's wake, fading with distance.
    *
    * The churn is a hash over (particle seed, glyph cell, churn step), not
    * a fresh random per frame: it has to be the *same* answer for every
    * node in a glyph cell until the next step, or the ring - where a glyph
    * is two or three isolated pixels - reads as strobing instead of as
    * characters changing.
    */
    class Pattern_Generic_MatrixRain : public PatternScanner
    {
    public:
        Pattern_Generic_MatrixRain();

        /// stage units per second, before the per-drop scatter. Live: it
        /// rescales the falling drops, not just the next ones.
        float fallSpeed = 12.0f;
        /// tail length in stage units
        float tailLength = 12.0f;
        /// a drop's radius - its lateral reach, and its head's glow
        float dropWidth = 0.9f;
        /// how many drops are in the air
        float dropCount = 14.0f;
        /// how hard the glyph churn chews the tail: 0 is a smooth streak
        float flicker = 0.4f;
        /// glyph changes per second
        float churnRate = 8.0f;
        /// how far each drop's hue strays from the green, as a share of the
        /// wheel: 0 is the film's monochrome, 1 is every drop its own colour.
        /// Per drop rather than per glyph - a streak that changed colour
        /// along its tail would read as several drops
        float hueSpread = 0.0f;

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        eanim::ParticleSystem drops;
    };


    /* @brief Fire 2012, reimagined on the particle system: the floor burns,
    * and the flames are risers.
    *
    * Kriegsman's grid sim (the one WLED ships) is a per-strip diffusion; on
    * the stage it kept fighting the geometry - integer columns, a bed that
    * either strobed or froze. Particles fit what this fire actually is
    * here: embers ignite *anywhere along the foot of the stage* - a
    * continuous line, not eight columns - rise with a lateral wander, cool
    * from white-yellow through orange to red as they age, and gutter out
    * partway up the obelisk. The bed itself is a distance-to-floor field
    * with a slow flicker, so the obelisk's lowest run and the truss beside
    * it hold a breathing ember floor while the risers climb through the
    * ring and on up the runs.
    *
    * The knobs keep their Fire2012 names and feel: cooling is how fast a
    * flame dies (so higher cooling is a shorter fire), sparking is how
    * eagerly the bed spawns risers, spread is their sideways wander.
    */
    class Pattern_Generic_Fire2012 : public PatternScanner
    {
    public:
        Pattern_Generic_Fire2012();

        float cooling = 30.0f;
        float sparking = 0.47f;
        /// lateral wander of a rising flame: 0 is candle-straight
        float spread = 0.15f;
        /// how fast a flame climbs, stage units per second
        float riseSpeed = 12.0f;
        /// a gain on the ignition, over sparking: 0 lights nothing new and
        /// the flames in the air burn out, which is how a fire is put out
        /// rather than switched off - record_arm runs this down on its cue
        float emitter = 1.0f;

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        eanim::ParticleSystem flames;
        /// fractional spawns carried between ticks, so the spawn rate is a
        /// rate rather than a per-frame coin flip
        float spawnDebt{0.0f};
    };


    /* @brief Flow, from WLED: palette bands flowing in alternating
    * directions.
    *
    * The stage's height is cut into zones; each zone carries the whole
    * palette as a gradient, flipped in every other zone, and the gradients
    * scroll - so neighbouring bands stream toward and away from each other
    * like layered currents. Zones run along y, which on this stage means
    * the flow pours through the ring and up the obelisk as one column of
    * currents.
    */
    class Pattern_Generic_Flow : public PatternScanner
    {
    public:
        HSVPalette flowPalette {
            HSV(285.0f, 0.85f, 0.9f),   // violet
            HSV(215.0f, 0.9f, 0.9f),    // blue
            HSV(165.0f, 0.8f, 0.85f),   // teal
        };

        /// palette cycles per second
        float flowSpeed = 0.25f;
        /// bands across the stage's height
        float zoneCount = 3.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief Lake, from WLED: still water, read from above.
    *
    * Two interfering waves - a cosine and a cubic-crested sine, at
    * different spatial scales and drifting phases - plus a slow threshold
    * that swallows the troughs, so the surface glints where the crests
    * align and goes deep-dark between them. The waves run diagonally
    * across (x, y) rather than along the wiring, which is what makes the
    * ring shimmer as a circle of water instead of a chase.
    */
    class Pattern_Generic_Lake : public PatternScanner
    {
    public:
        HSVPalette lakePalette {
            HSV(225.0f, 1.0f, 0.45f),   // deep water
            HSV(205.0f, 0.85f, 0.8f),   // mid
            HSV(185.0f, 0.6f, 1.0f),    // glint
        };

        float waveSpeed = 1.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief Pacifica, from WLED: Kriegsman and Garcia's gentle ocean.
    *
    * Four layers of palette waves at different spatial scales, speeds and
    * directions, summed; where the swell stacks high enough, whitecaps
    * lift the colour toward foam, and a floor keeps even the troughs
    * blue-green rather than black. The layers cross (x, y) diagonally in
    * both directions, so the interference drifts around the ring and
    * across the obelisk's face instead of marching up it.
    */
    class Pattern_Generic_Pacifica : public PatternScanner
    {
    public:
        float waveSpeed = 1.0f;
        /// how high the swell must stack before it foams, 0..1
        float capLevel = 0.72f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief Phased, from WLED: a wave whose wavelength breathes.
    *
    * One sine over the stage whose spatial frequency is itself modulated by
    * a slower sine - the bands stretch and squeeze as they travel, which is
    * the whole "phased" look. A slight x term tilts the bands so they drift
    * around the ring rather than hitting it as flat lines.
    */
    class Pattern_Generic_Phased : public PatternScanner
    {
    public:
        HSV waveColor = HSV(190.0f, 0.85f, 1.0f);

        float waveSpeed = 1.0f;
        /// base bands per stage, before the breathing
        float density = 0.5f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief Saw, from WLED: a sawtooth ramp sweeping the stage.
    *
    * Brightness climbs along y and snaps back, teeth times over the stage's
    * height, the whole comb scrolling - through the ring first, then up the
    * obelisk, since that is what the stage's y means.
    */
    class Pattern_Generic_Saw : public PatternScanner
    {
    public:
        HSV sawColor = HSV(30.0f, 1.0f, 1.0f);

        /// sweeps per second
        float sawSpeed = 0.5f;
        float teeth = 2.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief Spots Fade, from WLED - grown into the plane.
    *
    * WLED's version is evenly spaced spots on a strip, breathing. Here a
    * spot is a real point in (x, y): it swells in, glows as a soft radial
    * pool over whatever geometry is near it - a patch of the obelisk's
    * face, a bend of the ring's arc - fades out, and re-appears somewhere
    * else. Each spot keeps its own clock, so the stage carries a drifting
    * constellation rather than a metronome.
    */
    class Pattern_Generic_SpotsFade : public PatternScanner
    {
    public:
        HSV spotColor = HSV(45.0f, 0.75f, 1.0f);

        float spotCount = 6.0f;
        /// a spot's reach, in stage units
        float radius = 4.0f;
        /// seconds for one swell-and-fade, before the per-spot scatter
        float fadeSeconds = 3.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief TwinkleUp, from WLED: a starfield of independent twinkles.
    *
    * Each little patch of the stage - hashed from its (x, y), never its
    * wiring order - keeps its own phase and rate, and only the fraction
    * `density` allows twinkles at all. So the ring and the obelisk carry
    * one field of stars, each blinking on its own clock, with a touch of
    * hashed hue drift so they are not all the same white.
    */
    class Pattern_Generic_TwinkleUp : public PatternScanner
    {
    public:
        HSV starColor = HSV(50.0f, 0.25f, 1.0f);

        float twinkleSpeed = 1.0f;
        /// the fraction of the stage that twinkles at all
        float density = 0.5f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief Waterfall: sheets of water pouring down the stage.
    *
    * Two octaves of value noise scrolled downward, so the falling texture
    * is continuous in both axes - streaks slide down the obelisk's face,
    * through the ring's circle, and off the bottom. Brighter where the
    * water bunches, tinted from deep blue toward white foam at the crests.
    */
    class Pattern_Generic_Waterfall : public PatternScanner
    {
    public:
        HSV waterColor = HSV(205.0f, 0.9f, 1.0f);

        /// stage units per second, downward
        float fallSpeed = 10.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief Color Clouds: slow drifting fields of colour.
    *
    * Two noise fields crawling across (x, y) at different rates - one for
    * the cloud bodies, one for their hue - so soft patches of colour form,
    * drift over the ring and up the obelisk, and dissolve into each other.
    * The whole hue wheel wanders through over time; nothing repeats on any
    * clock you can hear.
    */
    class Pattern_Generic_ColorClouds : public PatternScanner
    {
    public:
        float driftSpeed = 1.0f;
        /// spatial zoom: higher is smaller, busier clouds
        float cloudScale = 1.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief The classic rainbow, on the stage: the wheel laid over its
    * height, scrolling - through the ring, up the obelisk. */
    class Pattern_Generic_Rainbow : public PatternScanner
    {
    public:
        /// wheels per second past a fixed point
        float wheelSpeed = 0.15f;
        /// how many wheels the stage's height holds
        float wheelCount = 1.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief The classic chase, on the stage: a bright head climbing the
    * height with a tail behind it, wrapping bottom to top. */
    class Pattern_Generic_Chase : public PatternScanner
    {
    public:
        HSV chaseColor = HSV(0.0f, 0.0f, 1.0f);

        /// laps of the stage per second
        float lapSpeed = 0.4f;
        /// the tail, as a fraction of the stage
        float tailFraction = 0.35f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };
}
