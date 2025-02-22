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
/// Patterns made for the obelisk
///

using namespace ecore;
using namespace eanim;

#define WALL_SIDE_LENGTH 42
#define WALL_SEGMENTS 2



inline HSVPalette p_summer {
    HSV(4.0f, 0.68f, 0.81f),
    HSV(47.0f, 0.76f, 0.94f),
};

inline HSVPalette p_fall {
    HSV(17.0f, 0.74f, 0.71f),
    HSV(16.0f, 0.54f, 0.76f),
    HSV(38.0f, 0.58f, 0.84f),
};

inline HSVPalette p_winter {
    HSV(227.0f, 0.83f, 0.84f),
    HSV(238.0f, 0.45f, 0.99f),
    HSV(181.0f, 0.67f, 0.83f),
};

inline HSVPalette p_spring {
    HSV(100.0f, 0.91f, 0.60f),
    HSV(18.0f, 0.612f, 0.941f),
};

class Pattern_Obelisk_FourSeasons : public GeneratorHSV, public Tickable
{
public:
    PerlinNoiseGenerator2D coreNoise;

    Pattern_Obelisk_FourSeasons();

    void init();
    
    virtual void tick(float deltaTime) override;

    virtual void applyEffectLogic(HSVStripNode* node, HSV& InOutColor) const override;
private:
    float currentTime;
};