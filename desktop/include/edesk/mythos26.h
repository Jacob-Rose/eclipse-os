// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>

#include "relics/mythos26/mythos26_looks.h"

#include "edesk/state_machine.h"

///
/// The mythos26 show, as the desk runs it: the looks are content, in
/// src/relics/mythos26, and these are the state machines built from them.
///

namespace edesk
{
    using namespace mythos26;

    /// The show's sixteen cues, in the order a UI shows them: the fourteen
    /// looks and the two house states either side of them. The
    /// static pair and the beat flash that used to live here are cues on the
    /// generic machine and the audio bus. See makeGenericStateMachine and
    /// beatPulseState.
    std::unique_ptr<StateMachinePattern> makeMythos26StateMachine();

    /// A flash over the whole rig, for an additive layer: `off`, and `flash`
    /// - the beat pulse, white by default and a `color` knob away from
    /// anything else, its envelope drawable at the desk. What the spec calls
    /// the bpm flash layer: patched over the show at `blend: add`, so a cue
    /// that wants it turns it on and every other cue never sees it.
    std::unique_ptr<StateMachinePattern> makeFlashStateMachine();
}
