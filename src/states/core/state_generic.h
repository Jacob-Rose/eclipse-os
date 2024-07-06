// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/jcolors.h"
#include "../../lib/j/janim.h"

#include "../../lib/j/jpalettes.h"

class State_Generic : public State, public j::Tickable
{
public:
    State_Generic(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;
};