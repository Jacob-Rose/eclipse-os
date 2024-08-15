// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "state.h"

#include <memory>

#include "../lib/eanim/generator_hsv.h"

using namespace ecore;
using namespace eanim;

/*
* Just runs the provided pattern
*/
class State_Generic : public State
{
public:
    State_Generic(const char* InStateName);

    virtual void onStateBegin() override;

    virtual void tick() override;

    std::shared_ptr<GeneratorHSV> generator;
};