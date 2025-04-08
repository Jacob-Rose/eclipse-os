// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>
#include <list>

#include "../lib/esm/state.h"
#include "../lib/esm/state_generic.h"
#include "../lib/eio/relic.h"
#include "../lib/eanim/generator_hsv.h"

using namespace ecore;
using namespace eanim;
using namespace esm;

/*
* Just runs the provided pattern
*/
class State_Carousel : public State_GenericHSV
{
public:
    State_Carousel(const char* InStateName, RelicIO* inIO) : State_GenericHSV(InStateName, inIO) {}

    void triggerCycle();

protected:
    virtual void onStateChangeState(StateStatus inStatus);
    virtual void tick(float deltaTime) override;

    std::list<std::shared_ptr<GeneratorHSV>> generatorQueue;
};