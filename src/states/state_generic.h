// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "../lib/esm/state.h"
#include "../lib/eanim/generator_hsv.h"

using namespace ecore;
using namespace eanim;
using namespace esm;
using namespace std;

/*
* Just runs the provided pattern
*/
class State_Generic : public State
{
public:
    State_Generic(const char* InStateName);

protected:
    virtual void onStateBegin() override;
    virtual void tick(float deltaTime) override;

    shared_ptr<GeneratorHSV> generator;
};