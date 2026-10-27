// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <ebeat/beat_clock.h>

using namespace ebeat;

TEST(beat_clock_predicts_between_beats_and_holds_phase_across_a_tempo_change)
{
    BeatClock clock;
    clock.setBpm(120.0f, BeatSource::Internal, 0.0);   // a beat is 0.5s
    CHECK_NEAR(clock.beatSeconds(), 0.5, 1e-9);
    CHECK_NEAR(clock.beatPosition(0.25), 0.5, 1e-9);
    CHECK_NEAR(clock.beatPosition(1.25), 2.5, 1e-9);
    CHECK_NEAR(clock.timeSinceBeat(1.25), 0.25f, 1e-6f);

    // halfway through beat 2, the tempo drops: still halfway through beat 2
    clock.setBpm(60.0f, BeatSource::Internal, 1.25);
    CHECK_NEAR(clock.beatPosition(1.25), 2.5, 1e-9);
    CHECK_NEAR(clock.beatPosition(1.75), 3.0, 1e-9);   // the rest of the beat lasts half a second
    CHECK(!clock.isLocked(1.75));

    // free-run off holds just short of the next beat instead of rolling on
    clock.setFreeRun(false);
    CHECK(clock.beatPosition(100.0) < 3.0);
    CHECK(clock.beatPosition(100.0) > 2.99);
}

TEST(beat_clock_counts_beats_not_messages_once_settled)
{
    BeatClock clock;
    clock.setBpm(120.0f, BeatSource::Internal, 0.0);

    // settle: five beats on the grid at 0.5s
    double when = 1.0;
    for (int i = 0; i < 6; ++i, when += 0.5)
    {
        clock.markBeat(when, BeatSource::MidiNote);
    }
    CHECK(clock.isLocked(when));
    const long long before = static_cast<long long>(clock.beatPosition(when - 0.5));

    // the same beat sent twice does not move the count
    clock.markBeat(when - 0.5 + 0.02, BeatSource::MidiNote);
    CHECK_EQ(static_cast<long long>(clock.beatPosition(when - 0.4)), before);

    // a dropped message: the next beat lands two beats on, and counts as two
    clock.markBeat(when + 0.5, BeatSource::MidiNote);
    CHECK_EQ(static_cast<long long>(clock.beatPosition(when + 0.5)), before + 2);

    // one stray is ignored; a second in a row is the music having moved
    const long long steady = static_cast<long long>(clock.beatPosition(when + 0.5));
    clock.markBeat(when + 0.5 + 0.23, BeatSource::MidiNote);
    CHECK_EQ(static_cast<long long>(clock.beatPosition(when + 0.5 + 0.23)), steady);
    clock.markBeat(when + 0.5 + 0.46, BeatSource::MidiNote);
    CHECK_EQ(static_cast<long long>(clock.beatPosition(when + 0.5 + 0.46)), steady + 1);
    CHECK_EQ(clock.getSource() == BeatSource::MidiNote, true);
}

TEST(beat_clock_taps_restarts_bars_and_leads_by_the_latency)
{
    BeatClock clock;
    clock.setBpm(120.0f, BeatSource::Internal, 0.0);

    // a tap is always a beat, grid or no grid, and teaches the tempo
    clock.markBeat(10.0, BeatSource::Manual);
    clock.markBeat(10.6, BeatSource::Manual);
    CHECK(clock.getBpm() < 120.0f);
    CHECK(clock.getBpm() > 100.0f);

    // a restart is the one: bars count from it
    clock.setBpm(120.0f, BeatSource::Internal, 20.0);
    clock.restart(20.0, BeatSource::Manual);
    CHECK_EQ(clock.beatInBar(20.1), 0);
    CHECK_EQ(clock.beatInBar(20.6), 1);
    CHECK_EQ(clock.beatInBar(21.6), 3);
    CHECK_EQ(clock.beatInBar(22.1), 0);
    CHECK_EQ(clock.beatsSinceDownbeat(22.1), 4);

    // latency tells readers the beat early, without moving the grid
    clock.setLatency(0.1);
    CHECK_NEAR(clock.beatPosition(20.4) - clock.beatPosition(20.3), 0.2, 1e-9);
    const double led = clock.beatPosition(21.0);
    clock.setLatency(0.0);
    CHECK_NEAR(led - clock.beatPosition(21.0), 0.2, 1e-9);
    clock.setLatency(60.0);
    CHECK_NEAR(clock.getLatency(), 2.0, 0.0);   // clamped to something sane

    // a held tempo takes beats for phase only
    clock.setLatency(0.0);
    clock.setTempoHeld(true);
    const float stated = clock.getBpm();
    clock.markBeat(30.0, BeatSource::Osc);
    clock.markBeat(30.9, BeatSource::Osc);
    CHECK_NEAR(clock.getBpm(), stated, 1e-6f);
}
