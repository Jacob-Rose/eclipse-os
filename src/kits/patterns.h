// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../lib/ecore/core.h"
#include "../lib/ecore/hsv.h"

#include "../lib/eio/strip_projection.h"

#include "palettes.h"
#include "effects.h"

#include "../lib/eanim/noise.h"
#include "../lib/external/FastNoiseLite.h"

///
/// Patterns are made to be a drag/drop solution to reuse complex patterns later
///

using namespace ecore;
using namespace eanim;
using namespace eio;


// similar to the last of us menu
// features:
// - dust particles from a palette
// - background from a palette
class Pattern_SpaceDust : public GeneratorHSV
{
public:
    HSVPalette mainPalette;
    HSVPalette dustPalette;
    //DropGenerator dropGenerator;

    void init();

    virtual void tick(float deltaTime) override;

    virtual void render(HSVStripNode* node, HSV& InOutColor) const override;
};

class Pattern_RitualFire : public GeneratorHSV
{
public:
    HSVPalette mainPalette = jpalettes::p_bootgradient;
    HSV gasColor = HSV(0.0f, 0.84f, 0.88f);
    HSVPalette firePalette = jpalettes::p_disney100;
    FireGenerator fireGenerator;
    PerlinNoiseGenerator2D pulsePerlinNoise;
    PerlinNoiseGenerator2D gasesPerlinNoise;

    Pattern_RitualFire();

    void init();
    
    virtual void tick(float deltaTime) override;

    virtual void render(HSVStripNode* node, HSV& InOutColor) const override;
private:
    float currentTime;
};

