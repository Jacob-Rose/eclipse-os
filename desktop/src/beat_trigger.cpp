// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/beat_trigger.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "edmx/beat_clock.h"

using namespace edmx;

float edmx::snapPulseRate(float pulsesPerBeat)
{
    // Snap to the nearest of the four rather than clamping to the range. A
    // slider spans 0.25..2 and will hand over 1.37 on the way past; the musical
    // answer to that is one of the four, never 1.37.
    if (pulsesPerBeat < 0.375f) return kQuarterTime;
    if (pulsesPerBeat < 0.75f)  return kHalfTime;
    if (pulsesPerBeat > 1.5f)   return kDoubleTime;
    return kOnBeat;
}

const char* edmx::triggerNameForRate(float pulsesPerBeat)
{
    const float rate = snapPulseRate(pulsesPerBeat);
    if (rate == kQuarterTime) return "bar";
    if (rate == kHalfTime)    return "half";
    if (rate == kDoubleTime)  return "double";
    return "beat";
}

int TriggerRack::beatsPerHit(float rate)
{
    if (rate <= kQuarterTime) return BeatClock::kBeatsPerBar;
    if (rate <= kHalfTime)    return 2;
    return 1;
}

TriggerRack::TriggerRack()
    : clock(&sharedBeatClock())
{
    // Slowest first, which is the order a desk lists them in.
    const float rates[] = {kQuarterTime, kHalfTime, kOnBeat, kDoubleTime};
    for (float rate : rates)
    {
        BeatTrigger trigger;
        trigger.name = triggerNameForRate(rate);
        trigger.rate = rate;
        triggers.push_back(trigger);
    }
}

void TriggerRack::armEntry(const std::string& name)
{
    if (std::find(armed.begin(), armed.end(), name) == armed.end())
    {
        armed.push_back(name);
    }
}

void TriggerRack::tick(double now)
{
    for (BeatTrigger& trigger : triggers)
    {
        evaluate(trigger, now);
    }
    armed.clear();
}

void TriggerRack::evaluate(BeatTrigger& trigger, double now)
{
    trigger.fired = false;
    trigger.sinceHit = 0.0f;

    const double position = clock->beatPosition(now);
    const long long beat = static_cast<long long>(std::floor(position));
    const double phase = position - std::floor(position);

    const int span = beatsPerHit(trigger.rate);

    // Asked for by a look coming up. Fires whatever the grid says, because a
    // cue that comes up dark reads as a cue that did not come up - and fires it
    // on the *shared* trigger, so every look bound here comes up together
    // rather than each seating a private counter on its own entry.
    const bool entry = std::find(armed.begin(), armed.end(), trigger.name) != armed.end();

    if (!trigger.started)
    {
        // Seat on the grid without firing. The rack starts with the process,
        // long before anything is looking at it, so there is nothing here worth
        // showing and a hit would only put the count somewhere arbitrary.
        trigger.started = true;
        trigger.lastBeat = beat;
        trigger.offbeatFired = (phase >= 0.5);
    }
    else if (beat != trigger.lastBeat)
    {
        trigger.lastBeat = beat;
        trigger.offbeatFired = false;

        // Off the bar rather than off a count of our own. Which beat of the bar
        // it is, is one question with one answer, and it belongs where the
        // beats are counted - the clock knows a message that arrived twice is
        // one beat, and a look watching the number at frame rate cannot. Half
        // time takes the one and the three of that bar; quarter time takes the
        // one.
        if (span <= 1 || (clock->beatInBar(now) % span) == 0)
        {
            trigger.fired = true;
            trigger.sinceHit = clock->timeSinceBeat(now);
        }
    }
    else if (trigger.rate >= kDoubleTime && !trigger.offbeatFired && phase >= 0.5)
    {
        // Double time's other hit, off the phase inside the beat rather than
        // off any counting at all, so it cannot drift from the beat it belongs
        // to. One per beat: the flag is cleared by the beat above.
        trigger.offbeatFired = true;

        trigger.fired = true;
        trigger.sinceHit = static_cast<float>((phase - 0.5) * clock->beatSeconds());
    }

    if (entry && !trigger.fired)
    {
        trigger.fired = true;
        trigger.sinceHit = 0.0f;
    }

    if (trigger.fired)
    {
        ++trigger.hits;
    }
}

const BeatTrigger& TriggerRack::forRate(float pulsesPerBeat) const
{
    const float rate = snapPulseRate(pulsesPerBeat);
    for (const BeatTrigger& trigger : triggers)
    {
        if (trigger.rate == rate)
        {
            return trigger;
        }
    }
    return triggers.front(); // unreachable: the four are built in the ctor
}

const BeatTrigger* TriggerRack::find(const std::string& name) const
{
    for (const BeatTrigger& trigger : triggers)
    {
        if (trigger.name == name)
        {
            return &trigger;
        }
    }
    return nullptr;
}

std::string TriggerRack::describe(double now) const
{
    std::ostringstream out;
    out << "beat=" << static_cast<long long>(std::floor(clock->beatPosition(now)))
        << " bar=" << clock->beatInBar(now);

    out << " fired=";
    bool any = false;
    for (const BeatTrigger& trigger : triggers)
    {
        if (!trigger.fired) continue;
        if (any) out << ",";
        out << trigger.name;
        any = true;
    }
    if (!any) out << "-";

    return out.str();
}

TriggerRack& edmx::sharedTriggerRack()
{
    static TriggerRack rack;
    return rack;
}
