// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/beat_clock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace edmx;

double edmx::nowSeconds()
{
    using clock = std::chrono::steady_clock;
    static const clock::time_point origin = clock::now();
    return std::chrono::duration<double>(clock::now() - origin).count();
}

const char* edmx::describeBeatSource(BeatSource source)
{
    switch (source)
    {
        case BeatSource::MidiClock: return "midi_clock";
        case BeatSource::MidiNote:  return "midi_note";
        case BeatSource::Manual:    return "manual";
        case BeatSource::Internal:  break;
    }
    return "internal";
}

// ============================================================================
// BeatClock
// ============================================================================

void BeatClock::setBpm(float bpm, BeatSource inSource, double when)
{
    const float clamped = std::clamp(bpm, kMinBpm, kMaxBpm);
    const double newPeriod = 60.0 / static_cast<double>(clamped);

    // Re-anchor so the beat in progress stays where it is. Without this a
    // tempo nudge mid-beat yanks the phase, and the rig stutters at exactly
    // the moment someone is trying to dial the tempo in.
    const double position = beatPosition(when);
    const double fraction = position - std::floor(position);

    period.store(newPeriod);
    beatNumber.store(static_cast<long long>(std::floor(position)));
    anchor.store(when - (fraction * newPeriod));
    source.store(static_cast<int>(inSource));
}

void BeatClock::setBpm(float bpm, BeatSource inSource)
{
    setBpm(bpm, inSource, nowSeconds());
}

float BeatClock::getBpm() const
{
    return static_cast<float>(60.0 / period.load());
}

void BeatClock::markBeat(double when, BeatSource inSource)
{
    const double last = anchor.load();

    // Learn the tempo from the gap, but only from a gap that could actually be
    // one beat. A dropped message, a pause, or a stray note-on all show up as
    // an interval way outside the range, and folding those into the average
    // would wreck a tempo that was fine.
    if (last > 0.0)
    {
        const double interval = when - last;
        if (interval >= (60.0 / kMaxBpm) && interval <= (60.0 / kMinBpm))
        {
            // Smoothed, not taken raw: MIDI arrives with a millisecond or two
            // of jitter and a rig that re-times off every single beat visibly
            // wobbles. The phase still snaps to each beat below; it is only the
            // *prediction* between beats that is averaged.
            period.store((period.load() * 0.75) + (interval * 0.25));
        }
    }

    // Guarantee the beat number moves, so a pattern watching for a change
    // always retriggers, and never moves backwards, so a counter shown in a UI
    // makes sense. Free-run may already have advanced past us.
    long long next = beatNumber.load() + 1;
    const long long predicted = static_cast<long long>(std::floor(beatPosition(when)));
    if (predicted >= next)
    {
        next = predicted + 1;
    }

    beatNumber.store(next);
    anchor.store(when);
    source.store(static_cast<int>(inSource));

    if (inSource != BeatSource::Internal)
    {
        lastExternal.store(when);
        externalBeats.fetch_add(1);
    }
}

void BeatClock::restart(double when, BeatSource inSource)
{
    // A restart says where the downbeat is, not how fast it is going, so the
    // period is left alone and only the phase moves.
    beatNumber.fetch_add(1);
    anchor.store(when);
    source.store(static_cast<int>(inSource));

    if (inSource != BeatSource::Internal)
    {
        lastExternal.store(when);
    }
}

double BeatClock::beatPosition(double now) const
{
    const double last = anchor.load();
    const long long number = beatNumber.load();

    // Nothing has ever driven us. Sit at the top of beat zero rather than
    // inventing a phase; the first markBeat starts the grid for real.
    if (last < 0.0)
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
        // Hold just short of the next beat. Clamping to exactly 1.0 would look
        // like a fresh beat to anything watching the integer part.
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

unsigned long long BeatClock::getExternalBeats() const
{
    return externalBeats.load();
}

std::string BeatClock::describe(double now) const
{
    char text[160];
    std::snprintf(text, sizeof(text),
                  "bpm=%.1f src=%s lock=%s free_run=%s beat=%lld",
                  static_cast<double>(getBpm()),
                  describeBeatSource(getSource()),
                  isLocked(now) ? "yes" : "no",
                  getFreeRun() ? "on" : "off",
                  static_cast<long long>(std::floor(beatPosition(now))));
    return std::string(text);
}

BeatClock& edmx::sharedBeatClock()
{
    static BeatClock instance;
    return instance;
}

// ============================================================================
// AudioLevel
// ============================================================================

const char* edmx::describeVuSource(VuSource source)
{
    switch (source)
    {
        case VuSource::Instant: return "inst";
        case VuSource::Meter:   return "meter";
        case VuSource::Average: break;
    }
    return "avg";
}

void AudioLevel::set(VuSource source, float inLevel, double when)
{
    Reading& reading = readings[static_cast<int>(source)];
    reading.level.store(std::clamp(inLevel, 0.0f, 1.0f));
    reading.stamp.store(when);
    reading.updates.fetch_add(1);
}

float AudioLevel::get(VuSource source, double now) const
{
    const Reading& reading = readings[static_cast<int>(source)];

    const double last = reading.stamp.load();
    if (last < 0.0)
    {
        return 0.0f; // never fed
    }

    const double age = now - last;
    if (age <= kHoldFor)
    {
        return reading.level.load(); // still current: hand it back untouched
    }
    if (age >= kStaleAfter)
    {
        return 0.0f;
    }

    // Past the hold and not yet dead: ramp out across what is left of the
    // window, so a source that stops fades rather than cutting to black.
    const double fade = (age - kHoldFor) / (kStaleAfter - kHoldFor);
    return reading.level.load() * static_cast<float>(1.0 - fade);
}

bool AudioLevel::isLive(VuSource source, double now) const
{
    const double last = readings[static_cast<int>(source)].stamp.load();
    return (last >= 0.0) && ((now - last) < kHoldFor);
}

unsigned long long AudioLevel::getUpdates(VuSource source) const
{
    return readings[static_cast<int>(source)].updates.load();
}

bool AudioLevel::isAnyLive(double now) const
{
    for (int index = 0; index < kVuSourceCount; ++index)
    {
        if (isLive(static_cast<VuSource>(index), now))
        {
            return true;
        }
    }
    return false;
}

std::string AudioLevel::describe(double now) const
{
    char text[128];
    std::snprintf(text, sizeof(text), "avg=%.2f inst=%.2f meter=%.2f",
                  static_cast<double>(get(VuSource::Average, now)),
                  static_cast<double>(get(VuSource::Instant, now)),
                  static_cast<double>(get(VuSource::Meter, now)));
    return std::string(text);
}

AudioLevel& edmx::sharedAudioLevel()
{
    static AudioLevel instance;
    return instance;
}
