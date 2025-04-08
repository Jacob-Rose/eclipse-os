// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "../lib/ecore/core.h"
#include "../lib/eio/relic.h"
#include "../lib/eio/screen_drawer.h"

#include "../lib/esm/state_generic.h"

using namespace ecore;
using namespace eio;

class State_PendantGeneric : public State_GenericHSV
{
public:
    State_PendantGeneric(const char* inStateName, RelicIO* inIO) : State_GenericHSV(inStateName, io) {}

    virtual void init();

    virtual void tick(float deltaTime) override;

};