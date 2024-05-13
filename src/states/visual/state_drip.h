// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/jcolors.h"

#include <list>
#include <ranges>

#define DRIP_ARRAY_ALPHA_ARRAY {0.0f, 0.1f, 0.3f, 0.7f, 0.9f, 1.0f, 0.9f, 0.7f, 0.3f, 0.1f, 0.0f}

struct DropInfo
{
    DropInfo(j::HSV inColor, float inDropSize);

    j::HSV color;
    float dropSize;
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
    virtual void tickScreen() override;

    float dropTime = 0.8f;
    std::pair<float, float> dropDelay = {0.4f, 0.8f};
    std::pair<float, float> dropSize = {3.0f, 5.0f};

    j::HSVPalette palette;

    ColorPickMode colorPickMode;

protected:
    std::list<DropInfo> drops;

    float timeSinceDrop;
    float nextDropTime;
};