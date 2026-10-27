// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "beat_clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ebeat;

const char* ebeat::describeBeatSource(BeatSource source)
{
    switch (source)
    {
        case BeatSource::MidiClock: return "midi_clock";
        case BeatSource::MidiNote:  return "midi_note";
        case BeatSource::Manual:    return "manual";
        case BeatSource::Osc:       return "osc";
        case BeatSource::Internal:  break;
    }
    return "internal";
}

void BeatClock::setBpm(float bpm, BeatSource inSource, double when)
{
    const float clamped = std::clamp(bpm, kMinBpm, kMaxBpm);
    const double newPeriod = 60.0 / static_cast<double>(clamped);

    // hold the phase: move the anchor back by the part of the beat already gone
    const double position = positionAt(when);
    const double fraction = position - std::floor(position);
    period.store(newPeriod);
    beatNumber.store(static_cast<long long>(std::floor(position)));
    anchor.store(when - (fraction * newPeriod));
    source.store(static_cast<int>(inSource));
}

float BeatClock::getBpm() const
{
    return static_cast<float>(60.0 / period.load());
}

void BeatClock::setTempoHeld(bool held)
{
    tempoHeld.store(held);
}

bool BeatClock::isTempoHeld() const
{
    return tempoHeld.load();
}

void BeatClock::learnPeriod(double interval)
{
    if (tempoHeld.load())
    {
        return;
    }
    if (interval < (60.0 / kMaxBpm) || interval > (60.0 / kMinBpm))
    {
        return;
    }
    period.store((period.load() * 0.75) + (interval * 0.25));
}

void BeatClock::takeBeat(double when, long long steps, Advance advance)
{
    long long next = lastBeatCount.load() + steps;
    const long long showing = static_cast<long long>(std::floor(positionAt(when)));
    if (advance == Advance::Grid)
    {
        next = std::max(next, showing);
    }
    else if (advance == Advance::Fresh)
    {
        next = std::max(next, showing + 1);
    }
    beatNumber.store(next);
    lastBeatCount.store(next);
    lastBeatAt.store(when);
    anchor.store(when);
    strayBeats.store(0);
}

void BeatClock::markBeat(double when, BeatSource inSource)
{
    const bool locked = isLocked(when);
    if (inSource != BeatSource::Internal)
    {
        lastExternal.store(when);
        externalBeats.fetch_add(1);
    }
    source.store(static_cast<int>(inSource));

    const double last = lastBeatAt.load();
    const double currentPeriod = period.load();
    const double elapsed = (last > 0.0) ? (when - last) : -1.0;

    // a tap or a detector is a run of beats that need not fit the old grid
    const bool machine = (inSource == BeatSource::MidiClock)
                      || (inSource == BeatSource::MidiNote);
    if (!machine)
    {
        settled.store(0);
        takeBeat(when, 1, Advance::Exact);
        learnPeriod(elapsed);
        return;
    }

    if (!locked || last <= 0.0)
    {
        settled.store(0);
        takeBeat(when, 1);
        learnPeriod(elapsed);
        return;
    }

    if (elapsed < (60.0 / static_cast<double>(kMaxBpm)))
    {
        return;   // too soon to be another beat: the same one, sent twice
    }

    const double beats = elapsed / currentPeriod;
    const long long steps = std::llround(beats);
    const double error = beats - static_cast<double>(steps);
    const bool agrees = std::fabs(error) <= kSnapWindow;

    if (settled.load() < kSettleBeats)
    {
        settled.store((agrees && steps == 1) ? (settled.load() + 1) : 0);
        takeBeat(when, 1);
        learnPeriod(elapsed);
        return;
    }

    if (!agrees)
    {
        if ((strayBeats.fetch_add(1) + 1) < kRelockBeats)
        {
            return;
        }
        settled.store(0);
        takeBeat(when, 1);
        learnPeriod(elapsed);
        return;
    }

    if (steps <= 0)
    {
        return;   // inside half a beat of the last one: the same beat again
    }
    takeBeat(when, steps);
    learnPeriod(elapsed / static_cast<double>(steps));
}

void BeatClock::restart(double when, BeatSource inSource)
{
    takeBeat(when, 1, Advance::Fresh);
    downbeat.store(lastBeatCount.load());
    settled.store(0);
    source.store(static_cast<int>(inSource));
    if (inSource != BeatSource::Internal)
    {
        lastExternal.store(when);
    }
}

double BeatClock::beatPosition(double now) const
{
    return positionAt(now + latency.load());
}

double BeatClock::positionAt(double now) const
{
    const double last = anchor.load();
    const long long number = beatNumber.load();
    if (std::isnan(last))
    {
        return static_cast<double>(number);
    }
    double phase = (now - last) / period.load();
    if (phase < 0.0)
    {
        phase = 0.0;
    }
    if (!freeRun.load() && phase > kHeldPhase)
    {
        phase = kHeldPhase;
    }
    return static_cast<double>(number) + phase;
}

float BeatClock::timeSinceBeat(double now) const
{
    const double position = beatPosition(now);
    const double fraction = position - std::floor(position);
    return static_cast<float>(fraction * period.load());
}

long long BeatClock::beatsSinceDownbeat(double now) const
{
    const long long beat = static_cast<long long>(std::floor(beatPosition(now)));
    return beat - downbeat.load();
}

int BeatClock::beatInBar(double now) const
{
    long long since = beatsSinceDownbeat(now) % kBeatsPerBar;
    if (since < 0)
    {
        since += kBeatsPerBar;   // a downbeat declared ahead of now
    }
    return static_cast<int>(since);
}

long long BeatClock::getDownbeat() const
{
    return downbeat.load();
}

double BeatClock::beatSeconds() const
{
    return period.load();
}

BeatSource BeatClock::getSource() const
{
    return static_cast<BeatSource>(source.load());
}

bool BeatClock::isLocked(double now) const
{
    const double last = lastExternal.load();
    return (last > 0.0) && ((now - last) < kExternalTimeout);
}

void BeatClock::setFreeRun(bool enable)
{
    freeRun.store(enable);
}

bool BeatClock::getFreeRun() const
{
    return freeRun.load();
}

void BeatClock::setLatency(double seconds)
{
    latency.store(std::clamp(seconds, -2.0, 2.0));
}

double BeatClock::getLatency() const
{
    return latency.load();
}

unsigned long long BeatClock::getExternalBeats() const
{
    return externalBeats.load();
}

std::string BeatClock::describe(double now) const
{
    char text[160];
    std::snprintf(text, sizeof(text),
                  "bpm=%.1f src=%s lock=%s free_run=%s beat=%lld bar_beat=%d latency=%.1fms",
                  static_cast<double>(getBpm()),
                  describeBeatSource(getSource()),
                  isLocked(now) ? "yes" : "no",
                  getFreeRun() ? "on" : "off",
                  static_cast<long long>(std::floor(beatPosition(now))),
                  beatInBar(now) + 1,
                  getLatency() * 1000.0);
    return std::string(text);
}
