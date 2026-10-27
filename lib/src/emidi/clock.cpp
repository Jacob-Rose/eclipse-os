// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "clock.h"

using namespace emidi;

void ClockFollower::reset()
{
    tickInBeat = 0;
    tickCount = 0;
    tickWrite = 0;
}

void ClockFollower::handle(const Message& message, double when, ClockSink& sink)
{
    switch (message.status)
    {
        case status::Clock:
        {
            ++ticks;
            const int tick = tickInBeat;
            tickInBeat = (tick + 1) % kClocksPerBeat;

            // the tick 24 ago was one beat ago by definition
            if (tickCount >= kClocksPerBeat)
            {
                const double beatSeconds = when - tickTimes[tickWrite];
                if (beatSeconds > 0.0)
                {
                    const float bpm = static_cast<float>(60.0 / beatSeconds);
                    if (bpm >= bpmMin && bpm <= bpmMax)
                    {
                        sink.onTempo(bpm, when);
                    }
                }
            }

            tickTimes[tickWrite] = when;
            tickWrite = (tickWrite + 1) % kClocksPerBeat;
            if (tickCount < kClocksPerBeat)
            {
                ++tickCount;
            }

            if (tick == 0)
            {
                ++beats;
                sink.onBeat(when);
            }
            return;
        }

        case status::Start:
            // the downbeat is here, and the window across the gap is stale
            tickInBeat = 0;
            tickCount = 0;
            tickWrite = 0;
            sink.onRestart(when);
            return;

        case status::SongPosition:
        {
            // sixteenth notes: four to a beat, six ticks to a sixteenth
            const int sixteenths = message.songPosition();
            tickInBeat = (sixteenths % 4) * 6;
            return;
        }

        case status::Continue:
        case status::Stop:
        default:
            return;
    }
}
