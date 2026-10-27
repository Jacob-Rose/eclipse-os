// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <ebeat/beat_clock.h>
#include <ebeat/beat_trigger.h>

using namespace ebeat;

TEST(beat_trigger_rates_snap_and_name_themselves)
{
    CHECK_NEAR(snapPulseRate(0.3f), kQuarterTime, 0.0f);
    CHECK_NEAR(snapPulseRate(0.6f), kHalfTime, 0.0f);
    CHECK_NEAR(snapPulseRate(1.37f), kOnBeat, 0.0f);
    CHECK_NEAR(snapPulseRate(1.6f), kDoubleTime, 0.0f);
    CHECK_EQ(std::string(triggerNameForRate(0.25f)), std::string("bar"));
    CHECK_EQ(std::string(triggerNameForRate(0.5f)), std::string("half"));
    CHECK_EQ(std::string(triggerNameForRate(1.0f)), std::string("beat"));
    CHECK_EQ(std::string(triggerNameForRate(2.0f)), std::string("double"));
}

TEST(beat_trigger_rack_fires_each_rate_on_its_beats_with_the_offset_inside_the_frame)
{
    BeatClock clock;
    clock.setBpm(120.0f, BeatSource::Internal, 0.0);   // beats at 0.0, 0.5, 1.0, ...
    clock.restart(0.0, BeatSource::Manual);            // beat 1 is the one... at position 1

    TriggerRack rack(clock);
    rack.tick(0.01);   // the first frame seats every trigger without firing
    for (const BeatTrigger& trigger : rack.all())
    {
        CHECK(!trigger.fired);
        CHECK(trigger.started);
    }

    // run two bars at 40 fps and count what fired
    int bar = 0, half = 0, beat = 0, dbl = 0;
    float firstBeatOffset = -1.0f;
    for (double now = 0.035; now < 4.0; now += 0.025)
    {
        rack.tick(now);
        if (rack.forRate(kQuarterTime).fired) ++bar;
        if (rack.forRate(kHalfTime).fired)    ++half;
        if (rack.forRate(kOnBeat).fired)
        {
            ++beat;
            if (firstBeatOffset < 0.0f) firstBeatOffset = rack.forRate(kOnBeat).sinceHit;
        }
        if (rack.forRate(kDoubleTime).fired)  ++dbl;
    }
    // the rack seats on beat 1, the one; beats 2..8 cross inside the window,
    // of which beat 5 is the next one and 3, 5, 7 are the half-time hits
    CHECK_EQ(beat, 7);
    CHECK_EQ(bar, 1);
    CHECK_EQ(half, 3);
    CHECK_EQ(dbl, 7 + 8);   // every beat plus every half-beat in the window
    CHECK(firstBeatOffset >= 0.0f);
    CHECK(firstBeatOffset < 0.025f);   // where inside the frame the beat landed

    CHECK(rack.find("half") != nullptr);
    CHECK(rack.find("nope") == nullptr);
    CHECK_EQ(rack.forRate(kOnBeat).hits, 7ull);
}

TEST(beat_trigger_entry_fires_once_off_the_grid)
{
    BeatClock clock;
    clock.setBpm(120.0f, BeatSource::Internal, 0.0);
    TriggerRack rack(clock);
    rack.tick(0.01);

    rack.armEntry("bar");
    rack.armEntry("bar");   // asking twice is asking once
    rack.tick(0.02);        // nowhere near a bar line
    CHECK(rack.forRate(kQuarterTime).fired);
    CHECK_NEAR(rack.forRate(kQuarterTime).sinceHit, 0.0f, 0.0f);
    CHECK_EQ(rack.forRate(kQuarterTime).hits, 1ull);
    CHECK(!rack.forRate(kOnBeat).fired);

    rack.tick(0.03);        // cleared by the tick that acted on it
    CHECK(!rack.forRate(kQuarterTime).fired);
}
