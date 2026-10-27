// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>
#include <limits>
#include <string>

namespace ebeat
{
    /// Where the current tempo came from, for status.
    enum class BeatSource
    {
        Internal,  ///< nothing external; the clock keeps its own time
        MidiClock, ///< 0xF8 realtime clock, 24 to the quarter note
        MidiNote,  ///< a note on each beat (DJ software)
        Manual,    ///< tapped in
        Osc,       ///< an audio detector over the network
    };

    const char* describeBeatSource(BeatSource source);

    /* @brief The beat grid: a tempo, a phase and a beat number that only counts
    * up. Something upstream marks beats; looks ask how far into the beat it is.
    *
    * It predicts: beats arrive twice a second, frames forty times, so the
    * position between beats is interpolated at the current tempo. It free-runs:
    * when the source goes quiet it keeps pulsing at the last tempo until a real
    * beat relocks it. And it counts beats, not messages: once kSettleBeats have
    * agreed with the grid, a beat is matched to it, so a dropped message moves
    * the count by two, a duplicated one by none, and a stray is ignored unless
    * kRelockBeats of them come in a row (a track change).
    *
    * One thread marks beats and another may read every frame: every field is
    * an atomic and nothing locks. `when` and `now` are seconds on any
    * monotonic clock, the same one throughout.
    *
    *     clock.setBpm(128.0f, BeatSource::Internal, now);
    *     clock.markBeat(when, BeatSource::MidiNote);   // from the wire
    *     const double position = clock.beatPosition(now);   // 12.25: a quarter into beat 12
    *
    * @see TriggerRack for which beats are hits
    * @see emidi::ClockFollower and emidi::BeatNotes for beats off a MIDI cable
    */
    class BeatClock
    {
    public:
        /// Tempos outside this are a mis-parse; gaps outside it are not learned from.
        static constexpr float kMinBpm = 30.0f;
        static constexpr float kMaxBpm = 300.0f;
        /// Seconds after the last external beat the clock still reports locked.
        static constexpr double kExternalTimeout = 2.0;
        /// How far off the predicted beat a message may land and still be it, in beats.
        static constexpr double kSnapWindow = 0.30;
        /// Agreeing beats before the grid is trusted to count off.
        static constexpr int kSettleBeats = 4;
        /// Off-grid beats in a row before the grid follows them.
        static constexpr int kRelockBeats = 2;
        static constexpr int kBeatsPerBar = 4;

        /// Sets the tempo at `when`, holding the phase: the beat in progress
        /// keeps its place, the next one lasts the new length.
        void setBpm(float bpm, BeatSource source, double when);
        float getBpm() const;

        /// While held, beats set the phase but the gaps between them stop
        /// teaching a tempo. For a source that states its bpm separately.
        void setTempoHeld(bool held);
        bool isTempoHeld() const;

        /// A beat landed at `when`. Re-anchors the phase, learns the tempo
        /// from the gap, and moves the count by however many beats went by.
        /// Manual and Osc beats are always taken as one beat.
        void markBeat(double when, BeatSource source);

        /// The downbeat is here and this is beat one: a MIDI Start, or an align.
        void restart(double when, BeatSource source);

        /// Beat number plus phase, led by the latency: 12.25 is a quarter of
        /// the way through beat 12. The integer part never goes backwards.
        double beatPosition(double now) const;
        long long beatsSinceDownbeat(double now) const;
        /// 0 is the one, 3 is the four. Counted from the last restart().
        int beatInBar(double now) const;
        long long getDownbeat() const;
        /// Seconds since the current beat started.
        float timeSinceBeat(double now) const;
        /// Seconds per beat.
        double beatSeconds() const;

        BeatSource getSource() const;
        /// External beats have arrived within kExternalTimeout.
        bool isLocked(double now) const;

        /// Keep predicting beats when the source goes quiet. On by default;
        /// off holds the phase just short of the next beat instead.
        void setFreeRun(bool enable);
        bool getFreeRun() const;

        /// Seconds every reader is told the beat early, to land light on the
        /// music after the frame, the wire and the fixture. Negative waits.
        /// Read side only: the grid itself stays in message time.
        void setLatency(double seconds);
        double getLatency() const;

        unsigned long long getExternalBeats() const;

        /// `bpm=128.0 src=midi_clock lock=yes free_run=on beat=417 bar_beat=3 latency=0.0ms`
        std::string describe(double now) const;

    private:
        static constexpr double kHeldPhase = 0.999;
        /// NaN, since a real anchor can be negative (a tempo set inside the first beat).
        static constexpr double kNoAnchor = std::numeric_limits<double>::quiet_NaN();

        void learnPeriod(double interval);
        /// beatPosition() without the latency lead.
        double positionAt(double now) const;

        enum class Advance
        {
            Grid,  ///< never below the prediction, so the count only goes up
            Fresh, ///< past it, so a look watching for a change always sees one
            Exact, ///< what the beats say, prediction or no prediction
        };
        void takeBeat(double when, long long steps, Advance advance = Advance::Grid);

        std::atomic<double> period{60.0 / 128.0};
        std::atomic<double> anchor{kNoAnchor};
        std::atomic<long long> beatNumber{0};
        /// The last beat accepted and its number; what markBeat() judges against.
        std::atomic<double> lastBeatAt{-1.0};
        std::atomic<long long> lastBeatCount{0};
        std::atomic<long long> downbeat{0};
        std::atomic<int> settled{0};
        std::atomic<int> strayBeats{0};
        std::atomic<double> lastExternal{-1.0};
        std::atomic<int> source{static_cast<int>(BeatSource::Internal)};
        std::atomic<bool> tempoHeld{false};
        std::atomic<bool> freeRun{true};
        std::atomic<unsigned long long> externalBeats{0};
        std::atomic<double> latency{0.0};
    };
}
