// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "beat_trigger.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "beat_clock.h"

using namespace ebeat;

float ebeat::snapPulseRate(float pulsesPerBeat)
{
    if (pulsesPerBeat < 0.375f) return kQuarterTime;
    if (pulsesPerBeat < 0.75f)  return kHalfTime;
    if (pulsesPerBeat > 1.5f)   return kDoubleTime;
    return kOnBeat;
}

const char* ebeat::triggerNameForRate(float pulsesPerBeat)
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

TriggerRack::TriggerRack(BeatClock& inClock)
    : clock(&inClock)
{
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
    const bool entry = std::find(armed.begin(), armed.end(), trigger.name) != armed.end();

    if (!trigger.started)
    {
        // seat on the grid without firing: entry is off-grid by construction
        trigger.started = true;
        trigger.lastBeat = beat;
        trigger.offbeatFired = (phase >= 0.5);
    }
    else if (beat != trigger.lastBeat)
    {
        trigger.lastBeat = beat;
        trigger.offbeatFired = false;
        if (span <= 1 || (clock->beatInBar(now) % span) == 0)
        {
            trigger.fired = true;
            trigger.sinceHit = clock->timeSinceBeat(now);
        }
    }
    else if (trigger.rate >= kDoubleTime && !trigger.offbeatFired && phase >= 0.5)
    {
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
    return triggers.front();   // unreachable: the four are built in the constructor
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
        << " bar=" << clock->beatInBar(now)
        << " fired=";
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
