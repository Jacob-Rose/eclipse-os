// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>
#include <ebeat/level_bus.h>

#include "relics/generic/audio_meter.h"

#include "edesk/state_machine.h"

namespace edesk
{
    using generic::Pattern_Audio_Meter;

    /// One cue per channel of the audio bus, in the bus's own order.
    ///
    /// Built from the channel table rather than from a list written here, so a
    /// channel added to AudioChannel is a cue on the rig and a pad on the
    /// surface with nothing else edited. Two lists that must agree is one list
    /// that eventually does not - see kAudioChannelNames.
    std::unique_ptr<StateMachinePattern> makeAudioStateMachine();
}
