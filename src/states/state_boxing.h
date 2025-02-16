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
#include "../kits/palettes.h"

using namespace ecore;
using namespace eanim;

#define ROW_LENGTH 1
#define COLUMN_LENGTH 200

#define LED_COUNT (ROW_LENGTH * COLUMN_LENGTH)

/*
* Just runs the provided pattern
*/
class State_Boxing_Noise : public State
{
public:
    PerlinNoiseGenerator2D coreNoise;

    ecore::HSVPalette palette = jpalettes::p_bootgradient;

    State_Boxing_Noise(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    std::shared_ptr<GeneratorHSV> generator;
};


class State_Boxing_Theater : public State 
{
public:
    LFO lfo;
    LFO paletteLFO;
    float timescale = 0.02f;

    ecore::HSVPalette palette = jpalettes::p_bootgradient;

    State_Boxing_Theater(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    std::shared_ptr<GeneratorHSV> generator;
};

class State_Boxing_Iterate : public State 
{
public:
    int idx = 0;
    float timescale = 0.1f;

    ecore::HSVPalette palette = jpalettes::p_bootgradient;

    State_Boxing_Iterate(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    float trackedDeltaTime = 0.0f;
};