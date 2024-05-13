// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/janim.h"

#include <AnimatedGIF.h>

class State_Emote_Heart : public State
{
public:
    State_Emote_Heart(const char* InStateName);

    virtual void init() override;

    virtual void tick() override;
    virtual void tickScreen() override;

    AnimatedGIF HeartGif;

    j::Saw heartSaw;

private:
    bool bLoadedHeart{false};

    float pulseAlpha = 0.0f;
};