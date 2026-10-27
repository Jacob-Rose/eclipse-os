// Copyright 2024 | Jake Rose
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "state.h"

#include <ecore/core.h>
#include <ecore/delegate.h>
#include <ecore/property.h>
#include <ecore/reflectable.h>
#include <eio/relic.h>
#include <eanim/generator_hsv.h>

using namespace ecore;
using namespace eanim;
using namespace esm;

/* @brief A state that runs one GeneratorHSV over every node of a RelicIO.
*
*     auto state = std::make_shared<State_GenericHSV>("blobs", io);
*     state->setGenerator(std::make_shared<Pattern_Obelisk_Blobs>());
*     state->init();
*/
class State_GenericHSV : public State, public ecore::Reflectable
{
public:
    State_GenericHSV(const char* InStateName, RelicIO* inIO) : State(InStateName), io(inIO) {}

    void setGenerator(std::shared_ptr<GeneratorHSV> inGenerator) { generator = inGenerator; }
    std::shared_ptr<GeneratorHSV> getGenerator() const { return generator; }

    /// Reflectable interface: the look's properties, or nothing without a look.
    virtual void reflect(ecore::PropertyBag& bag) override { if (generator) generator->reflect(bag); }

    /* @brief The colour a node starts from when the look renders it.
    *
    * LastFrame, the default: what the strip shows, so a look that writes only
    * some nodes leaves the rest, and one that reads before writing can trail
    * its own last frame. Black: every node starts dark each frame.
    */
    enum class RenderBase
    {
        LastFrame,
        Black,
    };

    void setRenderBase(RenderBase inBase) { renderBase = inBase; }
    RenderBase getRenderBase() const { return renderBase; }

    /// Advances the look's clocks without drawing. For a relic whose pixels
    /// a desk owns: the look keeps time, so the handback does not jump.
    void tickClocks(float deltaTime) { if (generator) generator->tick(deltaTime); }

protected:
    virtual void onStateChangeState(StateStatus inStatus);
    virtual void tick(float deltaTime) override;

    std::shared_ptr<GeneratorHSV> generator;
    RenderBase renderBase{RenderBase::LastFrame};
    RelicIO* io;
};

/* @brief A StateMachine that renders its states' nodes and blends them on
* the strip during a transition.
*/
class StateMachine_GenericHSV : public StateMachine
{
public:
    StateMachine_GenericHSV() = default;

    virtual void tick(float deltaTime) override;

    void setRelicIO(RelicIO* inIO) { io = inIO; }
    RelicIO* getRelicIO() const { return io; }

protected:
    /// The strip as it is now, into every node's snapshot buffer.
    virtual void captureBlendSource() override;

private:
    RelicIO* io{nullptr};

};