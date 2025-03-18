// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "state.h"
#include "../eio/relic.h"
#include "../eanim/generator_hsv.h"

using namespace ecore;
using namespace eanim;
using namespace esm;
using namespace std;

/*
* Just runs the provided pattern
*/
class State_GenericHSV : public State
{
public:
    State_GenericHSV(const char* InStateName, RelicIO* inIO) : io(inIO), State(InStateName) {}

    void setGenerator(shared_ptr<GeneratorHSV> inGenerator) { generator = inGenerator; }

protected:
    virtual void onStateChangeState(StateStatus inStatus);
    virtual void tick(float deltaTime) override;

    shared_ptr<GeneratorHSV> generator;
    RelicIO* io;
};

class StateMachine_GenericHSV : public StateMachine
{
public:
    StateMachine_GenericHSV() = default;

    virtual void tick(float deltaTime) override;

    void setRelicIO(RelicIO* inIO) { io = inIO; }
    RelicIO* getRelicIO() const { return io; }

private:
    RelicIO* io{nullptr};

};