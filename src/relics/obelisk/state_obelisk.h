// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "../../lib/esm/state.h"
#include "../../lib/eanim/lfo.h"
#include "../../lib/eanim/noise.h"
#include "../../lib/ecore/hsv.h"
#include "../../lib/eanim/generator_hsv.h"

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
};