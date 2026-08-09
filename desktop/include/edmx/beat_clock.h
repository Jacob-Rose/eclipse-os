// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>
#include <string>

///
/// Where the beat is.
///
/// A show that pulses on the music needs one number the whole rig agrees on:
/// how far through the current beat we are. That is all this is. Something
/// upstream — MIDI from the DJ software, or a tap, or nothing at all — says
/// "beat now" and how fast the beats are coming; patterns ask "how far into the
/// beat is it?" every frame and light accordingly.
///
/// Two properties are deliberate:
///
/// **It predicts.** Beats arrive twice a second; frames render forty times a
/// second. A pattern that only knew about beat *events* would have nothing to
/// animate between them, so the clock interpolates from the last beat at the
/// current tempo and hands back a continuous position.
///
/// **It free-runs.** If the MIDI link drops mid-set, the rig keeps pulsing at
/// the last known tempo instead of freezing. It will drift out of time with the
/// music, which is a much better failure than a dark stage, and it re-locks the
/// moment a real beat lands.
///

namespace edmx
{
    /// Seconds on a monotonic clock, zeroed at first use.
    ///
    /// Not wall time: the beat grid must not jump when the OS adjusts the
    /// clock, and this gets read on a MIDI callback thread where the cheapest
    /// correct thing is the right thing.
    double nowSeconds();

    /// Where the current tempo came from. Reported in status, so that "the
    /// lights are not on the beat" can be diagnosed without guessing.
    enum class BeatSource
    {
        Internal,  ///< nothing external; we are keeping our own time
        MidiClock, ///< 0xF8 realtime clock, 24 per quarter note
        MidiNote,  ///< a note-on per beat
        Manual,    ///< someone sent `beat`, i.e. tapped it in
    };

    const char* describeBeatSource(BeatSource source);

    /// The beat grid: a tempo, a phase, and a monotonic beat number.
    ///
    /// Written from the MIDI callback thread and read from the render thread,
    /// so every field is an atomic and nothing takes a lock. The fields are
    /// individually atomic rather than guarded as a group on purpose: a reader
    /// that catches a new period against an old anchor is out by a fraction of
    /// one frame, once, which is invisible — and it costs a MIDI callback
    /// nothing, which is where the real constraint is.
    class BeatClock
    {
    public:
        /// Tempos outside this are not music, they are a mis-parse. Intervals
        /// beyond the range are ignored rather than learned from.
        static constexpr float kMinBpm = 30.0f;
        static constexpr float kMaxBpm = 300.0f;

        /// How long after the last external beat we still call ourselves
        /// locked. Two seconds is several beats at any danceable tempo.
        static constexpr double kExternalTimeout = 2.0;

        /// Sets the tempo, holding the current phase. Changing tempo should not
        /// move the beat we are in the middle of; it should change how long the
        /// *next* one lasts.
        ///
        /// `when` is the moment to hold the phase at. It exists because the
        /// MIDI path already has a timestamp for the message and should not be
        /// mixing it with a second, slightly later reading of the clock — and
        /// because a test can then drive the whole thing in synthetic time.
        void setBpm(float bpm, BeatSource source, double when);
        void setBpm(float bpm, BeatSource source = BeatSource::Internal);
        float getBpm() const;

        /// A beat landed at `when` (a nowSeconds() value). Re-anchors the grid
        /// and learns the tempo from the gap since the last one.
        void markBeat(double when, BeatSource source);

        /// Start the grid over at `when` — the downbeat is here. This is what
        /// a MIDI Start means, and what a tap means.
        void restart(double when, BeatSource source);

        /// Beat number plus phase, as one continuous value: 12.25 is a quarter
        /// of the way through beat 12. Patterns trigger on the integer part
        /// changing and animate off the fraction.
        double beatPosition(double now) const;

        /// Seconds since the beat we are in started.
        float timeSinceBeat(double now) const;

        BeatSource getSource() const;

        /// True while beats are actually arriving from outside. False means the
        /// grid is our own — either free-running after a dropout, or never
        /// having been driven at all.
        bool isLocked(double now) const;

        /// Keep predicting beats when the external clock goes quiet. On by
        /// default: a dark rig is a worse failure than a drifting one.
        void setFreeRun(bool enable);
        bool getFreeRun() const;

        /// Beats seen from outside, for status and for spotting a link that is
        /// connected but silent.
        unsigned long long getExternalBeats() const;

        /// `bpm=128.0 src=midi_clock lock=yes beat=417`, for STATUS.
        std::string describe(double now) const;

    private:
        /// Phase is held just short of the next beat when free-run is off, so a
        /// stalled clock decays to a steady value instead of retriggering.
        static constexpr double kHeldPhase = 0.999;

        std::atomic<double> period{60.0 / 128.0};
        std::atomic<double> anchor{-1.0}; ///< when the current beat started
        std::atomic<long long> beatNumber{0};
        std::atomic<double> lastExternal{-1.0};
        std::atomic<int> source{static_cast<int>(BeatSource::Internal)};
        std::atomic<bool> freeRun{true};
        std::atomic<unsigned long long> externalBeats{0};
    };

    /// The process's beat clock.
    ///
    /// A singleton because of where the two ends live: patterns are built by
    /// factories that take no arguments and are handed no context, and the MIDI
    /// device runs on a callback thread that has no route to a pattern. There is
    /// exactly one beat in a room, so there is exactly one of these.
    BeatClock& sharedBeatClock();
}
