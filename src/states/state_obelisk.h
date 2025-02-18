// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once



#include <memory>

#include "../lib/esm/state.h"
#include "../lib/eanim/effects.h"
#include "../lib/ecore/hsv.h"
#include "../lib/eanim/generator_hsv.h"

using namespace ecore;
using namespace eanim;

#if 0

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
class State_Obelisk_FourSeasons : public State
{
public:
    PerlinNoiseGenerator2D coreNoise;

    std::vector<ecore::HSVPalette> palettes = {p_fall, p_winter, p_spring, p_neoncity};

    State_Obelisk_FourSeasons(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    std::shared_ptr<GeneratorHSV> generator;
};


class State_Obelisk_Theater : public State 
{
public:
    PerlinNoiseGenerator2D coreNoise;
    LFO lfo;
    LFO paletteLFO;
    float timescale = 0.02f;

    std::vector<ecore::HSVPalette> palettes = {p_fall, p_winter, p_spring, p_neoncity};

    State_Obelisk_Theater(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    std::shared_ptr<GeneratorHSV> generator;
};

#endif