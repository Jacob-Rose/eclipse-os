// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>

#include "message.h"

namespace emidi
{
    /* @brief What a ClockFollower reports: beats, tempo, and a restart. */
    class ClockSink
    {
    public:
        virtual ~ClockSink() = default;

        /// A beat landed at `when`: the 24th tick came round.
        virtual void onBeat(double when) = 0;

        /// The tempo, measured over the last 24 ticks. Once per tick, after
        /// one beat of history; bounded by the follower's bpm range.
        virtual void onTempo(float bpm, double when) = 0;

        /// Start (0xFA) arrived: the downbeat is here and the tempo window
        /// is stale.
        virtual void onRestart(double when) {  (void)when; }
    };

    /* @brief Follows MIDI beat clock: 24 ticks to the quarter note, Start and
    * Song Position for phase.
    *
    * Feed it every message (it ignores the ones that are not its); it reports
    * through a ClockSink. Tempo is the time between a tick and the tick 24
    * before it, which is exact after a single beat rather than settling over
    * many. Stop deliberately does nothing: the beat keeps running through a
    * track change.
    *
    *     follower.handle(message, when, sink);   // from a MessageSink
    *     follower.alignToNow();                  // a tap: the next tick is the beat
    */
    class ClockFollower
    {
    public:
        /// Tempos outside this range are measurement noise and not reported.
        void setBpmRange(float minBpm, float maxBpm) { bpmMin = minBpm; bpmMax = maxBpm; }

        void handle(const Message& message, double when, ClockSink& sink);

        /// Declares now a downbeat: the next tick is tick 0.
        void alignToNow() { tickInBeat = 0; }

        /// Puts the phase where something else says the beat is: a beat
        /// carried by a note makes the next tick 1, since one is consumed.
        void setTickInBeat(int tick) { tickInBeat = ((tick % kClocksPerBeat) + kClocksPerBeat) % kClocksPerBeat; }

        /// Tick within the beat, 0..23.
        int getTickInBeat() const { return tickInBeat; }
        uint32_t getTicks() const { return ticks; }
        uint32_t getBeats() const { return beats; }

        /// Forgets the window and the phase.
        void reset();

    private:
        float bpmMin{20.0f};
        float bpmMax{300.0f};

        int tickInBeat{0};
        uint32_t ticks{0};
        uint32_t beats{0};

        /// when each of the last 24 ticks arrived, so the tempo is a subtraction
        double tickTimes[kClocksPerBeat]{};
        int tickCount{0};
        int tickWrite{0};
    };
}
