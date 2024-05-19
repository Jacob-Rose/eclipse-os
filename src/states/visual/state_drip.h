// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/jcolors.h"
#include "../../lib/j/jpalettes.h"

#include <list>
#include <ranges>

#define DRIP_ALPHA_LERP {0.0f, 0.1f, 0.3f, 0.7f, 0.9f, 1.0f, 0.9f, 0.7f, 0.3f, 0.1f, 0.0f}

struct DropInfo
{
    DropInfo(j::HSV inColor, float inDropSize, int inLocation);

    j::HSV color;
    float dropSize;
    int location;
    float dropTime = 0.0f;
};

enum class ColorPickMode
{
    RandomPalette
};

class State_Drip : public State
{
public:
    State_Drip(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    float dropTime = 0.3f;
    std::pair<float, float> dropDelay = {0.002f, 0.012f};
    std::pair<float, float> dropSize = {2.0f, 4.0f};

    j::HSVPalette palette = j::p_iceCream;

protected:
    std::list<DropInfo> drops;

    float timeSinceDrop;
    float nextDropTime;
};