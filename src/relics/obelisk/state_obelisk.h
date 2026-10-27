// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include <esm/state.h>
#include <eanim/lfo.h>
#include <eanim/noise.h>
#include <ecore/hsv.h>
#include <ecore/property.h>
#include <eanim/generator_hsv.h>

#include "../../kits/effects.h"

using namespace ecore;
using namespace eanim;
using namespace eio;
using namespace esm;

#define WALL_SIDE_LENGTH 43

inline HSVPalette p_summer {
    HSV(87.0f, 0.59f, 0.67f),
    HSV(200.0f, 0.31f, 0.97f),
};

inline HSVPalette p_fall {
    HSV(51.0f, 0.42f, 0.95f),
    HSV(29.0f, 0.60f, 0.92f),
    HSV(7.0f, 0.75f, 0.95f),
};

inline HSVPalette p_winter {
    HSV(227.0f, 0.83f, 0.84f),
    HSV(238.0f, 0.45f, 0.99f),
    HSV(181.0f, 0.67f, 0.83f),
};

inline HSVPalette p_spring {
    HSV(196.0f, 0.10f, 0.97f),
    HSV(0.0f, 0.17f, 0.96f),
};

inline HSVPalette p_neoncity {
    HSV(255.0f, 1.0f, 0.39f),
    HSV(309.0f, 0.92f, 0.98f),
};

/*
* Just runs the provided pattern
*/
class Pattern_Obelisk_FourSeasons : public GeneratorHSV
{
public:
    Pattern_Obelisk_FourSeasons();

    PerlinNoiseGenerator2D coreNoise;

    std::vector<ecore::HSVPalette> palettes = {p_fall, p_winter, p_spring, p_neoncity};

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    virtual void reflect(ecore::PropertyBag& bag) override;
};


class Pattern_Obelisk_Monocolor : public GeneratorHSV
{
public:
    Pattern_Obelisk_Monocolor();

    HSV color = HSV(100.0f, 0.5f, 0.5f);

    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    virtual void reflect(ecore::PropertyBag& bag) override;
};

/*
* Blobs of an accent colour drifting through a primary one.
*
* The four-seasons look's noise field, generalised: one field over the whole
* stage rather than a palette per side, so it reads the same on the obelisk,
* the ring, a truss - anything with a coordinate - and both colours are knobs
* at the desk instead of a palette compiled in. The field is thresholded into
* blobs: `coverage` is how much of the stage the accent takes, `softness` how
* wide a blob's edge is, from a hard-edged cell to a full gradient.
*/
class Pattern_Obelisk_Blobs : public GeneratorHSV
{
public:
    Pattern_Obelisk_Blobs();

    PerlinNoiseGenerator2D noise;

    HSV primary = HSV(255.0f, 1.0f, 0.39f);   // p_neoncity's deep violet
    HSV accent = HSV(309.0f, 0.92f, 0.98f);   // and its magenta

    float scale = 0.05f;     // noise frequency: smaller is bigger blobs
    float coverage = 0.5f;   // 0..1, how much of the field is accent
    float softness = 0.3f;   // 0..1, edge width of a blob

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    virtual void reflect(ecore::PropertyBag& bag) override;
};

/*
* The golden key's look: the blobs field gone rainbow, with a sweep that
* spins round the tower.
*
* The base is the blobs' look with the rainbow for its gradient: the same
* Perlin field, drifting the same way, but where blobs blends two colours
* across it this runs it through `hue_spread` of the colour wheel - the
* field stretched by `contrast` so its bunched-up middle reaches the whole
* span - and the whole wheel drifts round once every `hue_cycle` seconds.
*
* Over it, `arms` bright sweeps go round the four sides by column, each
* trailing a tail `tail` of a turn long. Their speed is an LFO: they
* accelerate from a near stop to full speed and back down over
* `loop_seconds`, `laps` turns in each loop (rounded to whole laps, so the
* loop wraps seamlessly). The spin is one clock, `spin.time`, so `sim`
* carries all of it.
*/
class Pattern_Obelisk_Prism : public GeneratorHSV
{
public:
    Pattern_Obelisk_Prism();

    PerlinNoiseGenerator2D noise;

    float scale = 0.05f;         // noise frequency: smaller is bigger blobs
    float hueSpread = 1.0f;      // how much of the wheel the field's gradient spans, 0..1
    float contrast = 1.6f;       // stretches the field: noise sits near its middle, this spreads it
    float hueCycle = 40.0f;      // seconds for the wheel to drift round once
    float baseLevel = 0.65f;     // the field's brightness under the sweeps

    float loopSeconds = 16.0f;   // one acceleration and slowdown
    float laps = 3.0f;           // turns in a loop, rounded: whole, so it wraps clean
    float depth = 0.6f;          // 0 steady speed, 1 the slow end is a full stop
    float arms = 2.0f;           // sweeps, evenly round the tower (rounded)
    float tail = 0.4f;           // a sweep's tail, as a fraction of a turn
    float lead = 0.15f;          // its soft leading edge, a fraction of a turn: wider
                                 // than a column (an eighth), so it fades across
                                 // the columns instead of stepping onto each
    float whiten = 0.2f;         // how far the head washes toward white
    float twist = 0.0f;          // turns of lag from foot to tip: a spiral
    bool sweeps = true;          // off, the field alone - rainbow_blobs

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    virtual void reflect(ecore::PropertyBag& bag) override;

private:
    /// the loop's own clock, 0..loopSeconds
    float spinTime{0.0f};
    /// the hue's drift, 0..1
    float hueTime{0.0f};
    /// where the first sweep's head is this frame, in turns 0..1 - worked
    /// out once per tick from spinTime, not per pixel
    float headTurn{0.0f};

    void updateHead();
};

/*
* The golden key's first step: prism's rainbow field without the sweeps -
* the blobs gone rainbow, at the blobs' full brightness. The key's second
* scan brings the spin (prism), and the third goes home to the blobs.
*/
class Pattern_Obelisk_RainbowBlobs : public Pattern_Obelisk_Prism
{
public:
    Pattern_Obelisk_RainbowBlobs()
    {
        sweeps = false;
        baseLevel = 1.0f;
    }
};

class Pattern_Obelisk_Theater : public GeneratorHSV
{
public:
    LFO lfo;
    LFO paletteLFO;
    float timescale = 0.02f;

    std::vector<ecore::HSVPalette> palettes = {p_fall, p_winter, p_spring, p_neoncity};

    Pattern_Obelisk_Theater();

    virtual void tick(float deltaTIme) override;
    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    virtual void reflect(ecore::PropertyBag& bag) override;
};