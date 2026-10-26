// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "state.h"

#include "../ecore/core.h"
#include "../ecore/delegate.h"
#include "../ecore/property.h"
#include "../ecore/reflectable.h"
#include "../eio/relic.h"
#include "../eanim/generator_hsv.h"

using namespace ecore;
using namespace eanim;
using namespace esm;

/*
* Just runs the provided pattern
*/
class State_GenericHSV : public State, public ecore::Reflectable
{
public:
    State_GenericHSV(const char* InStateName, RelicIO* inIO) : State(InStateName), io(inIO) {}

    void setGenerator(std::shared_ptr<GeneratorHSV> inGenerator) { generator = inGenerator; }
    std::shared_ptr<GeneratorHSV> getGenerator() const { return generator; }

    /// Reflectable interface: the look's properties, or nothing for a state
    /// with no generator.
    virtual void reflect(ecore::PropertyBag& bag) override { if (generator) generator->reflect(bag); }

    /* @brief What a node's colour is when the look is asked to render it.
    *
    * LastFrame, the default: the colour the strip is showing, so a look
    * that only writes some nodes leaves the rest as they were, and one
    * that reads before it writes can fade or trail its own last frame.
    * Black: every node starts dark each frame, for a look that blends
    * into what it is handed and must not feed back on itself.
    */
    enum class RenderBase
    {
        LastFrame,
        Black,
    };

    void setRenderBase(RenderBase inBase) { renderBase = inBase; }
    RenderBase getRenderBase() const { return renderBase; }

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
    RenderBase renderBase{RenderBase::LastFrame};
    RelicIO* io;
};

class StateMachine_GenericHSV : public StateMachine
{
public:
    StateMachine_GenericHSV() = default;

    virtual void tick(float deltaTime) override;

    void setRelicIO(RelicIO* inIO) { io = inIO; }
    RelicIO* getRelicIO() const { return io; }

protected:
    /// The picture on the strip right now, into every node's snapshot buffer.
    virtual void captureBlendSource() override;

private:
    RelicIO* io{nullptr};

};