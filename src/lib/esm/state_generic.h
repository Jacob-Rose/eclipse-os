// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "state.h"

#include "../ecore/core.h"
#include "../ecore/delegate.h"
#include "../eio/relic.h"
#include "../eanim/generator_hsv.h"

using namespace ecore;
using namespace eanim;
using namespace esm;

/*
* Just runs the provided pattern
*/
class State_GenericHSV : public State
{
public:
    State_GenericHSV(const char* InStateName, RelicIO* inIO) : State(InStateName), io(inIO) {}

    void setGenerator(std::shared_ptr<GeneratorHSV> inGenerator) { generator = inGenerator; }
    std::shared_ptr<GeneratorHSV> getGenerator() const { return generator; }

    /// The look's running state, by name - see GeneratorHSV::reflectState.
    /// Empty for a state with no generator or a look with no clocks.
    void reflectState(ecore::PropertyBag& bag) { if (generator) generator->reflectState(bag); }

    /// Advances the look's clocks without drawing a pixel. For a relic whose
    /// pixels a desk owns: the look keeps time underneath the takeover, so
    /// the desk's simulation of it stays in step and the handback lands on
    /// the picture the desk was already showing. Cheap where tick() is not -
    /// no nodes are visited.
    void tickClocks(float deltaTime) { if (generator) generator->tick(deltaTime); }

protected:
    virtual void onStateChangeState(StateStatus inStatus);
    virtual void tick(float deltaTime) override;

    std::shared_ptr<GeneratorHSV> generator;
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