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

void BeatClock::learnPeriod(double interval)
{
    // Only from a gap that could actually be one beat. A dropped message, a
    // pause, or a stray note-on all show up as an interval way outside the
    // range, and folding those into the average would wreck a tempo that was
    // fine.
    if (interval < (60.0 / kMaxBpm) || interval > (60.0 / kMinBpm))
    {
        return;
    }

    // Smoothed, not taken raw: MIDI arrives with a millisecond or two of jitter
    // and a rig that re-times off every single beat visibly wobbles. The phase
    // still snaps to each beat below; it is only the *prediction* between beats
    // that is averaged.
    period.store((period.load() * 0.75) + (interval * 0.25));
}

void BeatClock::takeBeat(double when, long long steps, Advance advance)
{
    // Counted from the last beat we took, not from the number as it stands:
    // free-run has been moving that on its own between the two, and adding to
    // it would count the same beats twice.
    long long next = lastBeatCount.load() + steps;

    // What free-run has been predicting in the meantime, which is a beat the
    // rig has already lit and a number a status line has already shown.
    const long long showing = static_cast<long long>(std::floor(beatPosition(when)));

    if (advance == Advance::Grid)
    {
        // Never behind it: the prediction is on screen, and a count that went
        // backwards would read as a beat happening twice. The two only disagree
        // when a tempo change has left free-run predicting off a period the
        // music has stopped using.
        next = std::max(next, showing);
    }
    else if (advance == Advance::Fresh)
    {
        // Past it, so that a look watching the number for a change sees one.
        next = std::max(next, showing + 1);
    }
    // Advance::Exact takes the count the beats give it and lets the number sit
    // back down onto it if free-run had run ahead. That costs the never-
    // backwards guarantee for one beat and buys the thing that matters more -
    // one tap is one beat, so the pair a half-time look alternates between
    // survives a tap landing either side of where free-run guessed.

    beatNumber.store(next);
    lastBeatCount.store(next);
    lastBeatAt.store(when);
    anchor.store(when);
    strayBeats.store(0);
}

void BeatClock::markBeat(double when, BeatSource inSource)
{
    // Read before this beat is filed as external, or the beat that establishes
    // the lock would find that it was already locked.
    const bool locked = isLocked(when);

    if (inSource != BeatSource::Internal)
    {
        // Filed before the message is judged rather than after. One we go on to
        // throw away still says the link is alive, and a count that hid the
        // duplicates would hide the very thing this function exists to survive.
        lastExternal.store(when);
        externalBeats.fetch_add(1);
    }

    source.store(static_cast<int>(inSource));

    const double last = lastBeatAt.load();
    const double currentPeriod = period.load();

    // Negative when there is no last beat to measure from, which reads as "not
    // a tempo" everywhere it is used: learnPeriod() throws it out on range, and
    // nothing else looks at it without a grid to compare it against. Taken as a
    // gap it is a tap teaching the clock 40bpm on the first press.
    const double elapsed = (last > 0.0) ? (when - last) : -1.0;

    const bool machine = (inSource == BeatSource::MidiClock)
                      || (inSource == BeatSource::MidiNote);

    if (!machine)
    {
        // A tap is a person, and a person does not send the same beat twice. It
        // is also the only source allowed to disagree with the grid on purpose:
        // tapping a tempo in *is* a run of beats that do not fit the old one,
        // and measuring them against it would throw away the tempo being
        // tapped. So a tap goes straight in, one beat each, the way every beat
        // used to - and exactly one, even where free-run had already predicted
        // past it. The number moving *back* onto the tap is what makes the hit
        // land under the finger; taking the prediction instead would count one
        // tap as two beats and move a half-time look onto the other beat.
        settled.store(0);
        takeBeat(when, 1, Advance::Exact);
        learnPeriod(elapsed);
        return;
    }

    if (!locked || last <= 0.0)
    {
        // Nothing to measure against: no beat has ever arrived, or the link has
        // been quiet long enough that the grid is a guess. Take it as it comes
        // and start counting from here.
        settled.store(0);
        takeBeat(when, 1);
        learnPeriod(elapsed);
        return;
    }

    // Physics rather than tempo: kMaxBpm apart is the closest two beats can be,
    // so a message closer than that behind another is the same beat said twice.
    // Mixxx does this - a mapping that fires on a control change as well as a
    // note, a deck reporting the beat again across a loop boundary - and two
    // messages counted as two beats is the whole bug. Dropped whole: the phase
    // we have came from the first of the pair, which is the one that was right.
    if (elapsed < (60.0 / static_cast<double>(kMaxBpm)))
    {
        return;
    }

    // Where the grid says this message is. Rounded rather than floored: a beat
    // landing a hair early is this beat arriving early, not the last one
    // arriving very late.
    const double beats = elapsed / currentPeriod;
    const long long steps = std::llround(beats);
    const double error = beats - static_cast<double>(steps);
    const bool agrees = std::fabs(error) <= kSnapWindow;

    if (settled.load() < kSettleBeats)
    {
        // Still acquiring. The grid is not yet worth measuring anything
        // against, so this behaves the way it always did - one message, one
        // beat, learn the gap - which is what converges the tempo from a
        // standing start, or after the music has moved somewhere new.
        settled.store((agrees && steps == 1) ? (settled.load() + 1) : 0);
        takeBeat(when, 1);
        learnPeriod(elapsed);
        return;
    }

    if (!agrees)
    {
        // Nowhere near where a beat was due. One of these is the other deck
        // bleeding onto the same note, or a stray: following it would drag the
        // grid off the music every time it happened, so the grid stands and the
        // message is dropped. A run of them is not a stray - it is a track
        // change, or a deck swap - and then the grid is the thing that is
        // wrong, so we go back to acquiring one from scratch.
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
        return; // inside half a beat of the last one: the same beat again
    }

    takeBeat(when, steps);

    // Per beat, not per message: a beat whose message went missing leaves a gap
    // of two, and learning that as one beat would halve the tempo over it.
    // After the grid has moved rather than before: takeBeat() reads how far
    // free-run had got, and that has to be read at the period it ran at.
    learnPeriod(elapsed / static_cast<double>(steps));
}

void BeatClock::restart(double when, BeatSource inSource)
{
    // A restart says where the downbeat is, not how fast it is going, so the
    // period is left alone and only the phase moves.
    //
    // A fresh beat, not the one free-run is halfway through: someone aligning
    // the one mid-bar is asking for the hit *now*, and a look watching the
    // count for a change would sit through the rest of the bar otherwise.
    takeBeat(when, 1, Advance::Fresh);

    // And it says *which* beat this one is, which is the other half of the job:
    // this is the one. Everything counted in groups - half time, a bar - counts
    // from here, so a Start, or `midi align`, is how the rig gets told the thing
    // the music never says. Nothing else moves it.
    downbeat.store(lastBeatCount.load());

    // The grid moved on purpose, so what was learned about the old one is not
    // evidence about this one.
    settled.store(0);

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
        since += kBeatsPerBar; // only reachable if a downbeat is declared ahead
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

unsigned long long BeatClock::getExternalBeats() const
{
    return externalBeats.load();
}

std::string BeatClock::describe(double now) const
{
    char text[160];
    std::snprintf(text, sizeof(text),
                  "bpm=%.1f src=%s lock=%s free_run=%s beat=%lld bar_beat=%d",
                  static_cast<double>(getBpm()),
                  describeBeatSource(getSource()),
                  isLocked(now) ? "yes" : "no",
                  getFreeRun() ? "on" : "off",
                  static_cast<long long>(std::floor(beatPosition(now))),
                  beatInBar(now) + 1);
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
