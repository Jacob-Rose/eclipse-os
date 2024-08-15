// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../lib/ecore/hsv.h"
#include "../lib/eanim/effects.h"

#include "palettes.h"

#include "../lib/external/FastNoiseLite.h"

///
/// Patterns are made to be a drag/drop solution to reuse complex patterns later
///

using namespace ecore;
using namespace eanim;


// similar to the last of us menu
// features:
// - dust particles from a palette
// - background from a palette
class Pattern_SpaceDust : public GeneratorHSV, public Tickable
{
public:
    HSVPalette mainPalette;
    HSVPalette dustPalette;
    DropGenerator dropGenerator;

    void init();

    virtual void tick(float deltaTime) override;

    virtual void applyEffectLogic(std::vector<HSV>& InOutColors) const;
};

class Pattern_RitualFire : public GeneratorHSV, public Tickable
{
public:
    HSVPalette mainPalette = jpalettes::p_bootgradient;
    HSV gasColor = HSV(0.0f, 0.84f, 0.88f);
    HSVPalette firePalette = jpalettes::p_disney100;
    FireGenerator fireGenerator;
    PerlinNoiseGenerator1D pulsePerlinNoise;
    PerlinNoiseGenerator1D gasesPerlinNoise;

    Pattern_RitualFire();

    void init();
    
    virtual void tick(float deltaTime) override;

    virtual void applyEffectLogic(std::vector<HSV>& InOutColors) const override;
private:
    float currentTime;
};