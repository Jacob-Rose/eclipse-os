// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "beat_clock.h"
#include "beat_trigger.h"
#include "level_bus.h"

///
/// The process's beat, triggers and audio bus, and a clock to read them
/// against. One of each, because there is one beat in a room: looks are
/// built by factories that take no arguments, and whatever marks the beat
/// (a MIDI thread, an OSC port, a UART) has no route to a look.
///

namespace ebeat
{
    /// Seconds on a monotonic clock, zeroed at first use. millis() on a
    /// board, a steady clock on a host; never wall time, so the beat grid
    /// does not jump when the OS adjusts the clock.
    double nowSeconds();

    BeatClock& sharedBeatClock();
    /// On sharedBeatClock().
    TriggerRack& sharedTriggerRack();
    AudioLevel& sharedAudioLevel();
}
