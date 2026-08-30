// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>
#include <string>

///
/// What the music is doing: where the beat is, and how loud it is.
///
/// Two things rather than one file per thing, because they are the same shape
/// and arrive down the same cable. Both are written by the MIDI callback thread
/// and read by the render thread every frame, both are lock-free atomics for
/// that reason, and both are process-wide singletons because the pattern
/// factories take no arguments and the MIDI thread has no route to a pattern.
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
/// **It counts beats, not messages.** What arrives on the cable is not one
/// clean beat per beat: Mixxx sends the same beat twice, sends nothing for the
/// next one, and puts the other deck's beats over the top of this one's. A beat
/// message is therefore not taken as a beat — it is matched against the grid we
/// already have, and the count moves by however many beats have actually gone
/// by, which is two across a dropped message and none across a duplicated one.
/// Everything slower than the beat is counted off that number, so a message
/// miscounted is a half-time look moved onto the other half of the pair, where
/// it stays until the next miscount moves it back. See markBeat().
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

        /// How far off the beat the grid predicted a message may land and still
        /// be that beat, as a fraction of one beat. Generous, because what it
        /// is allowing for is a tempo estimate a few percent out; a message
        /// further away than this is not this grid's beat at all.
        static constexpr double kSnapWindow = 0.30;

        /// Beats that agree with the grid before it is trusted enough to count
        /// off. Until then a message is taken as one beat and the tempo is
        /// learned from it, which is how the grid gets acquired in the first
        /// place — and is what it always used to do.
        static constexpr int kSettleBeats = 4;

        /// Beats landing nowhere near the grid, in a row, before we believe
        /// them. One is a stray or the other deck; two is a track change, and
        /// the rig has to follow the music rather than argue with it.
        static constexpr int kRelockBeats = 2;

        /// Beats to the bar. See beatInBar().
        static constexpr int kBeatsPerBar = 4;

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

        /// A beat message landed at `when` (a nowSeconds() value). Re-anchors
        /// the grid, learns the tempo, and moves the count on by however many
        /// beats went by — which is the part that is not obvious.
        ///
        /// The naive version of this — one message, one beat, always — is what
        /// was here, and it is wrong in both directions. Mixxx sends a beat
        /// twice and the count gains one; a message goes missing and the count
        /// loses one. Either way everything counted in groups off that number
        /// (half time, a bar) lands on a different member of the group from
        /// then on, which on a rig reads as a look randomly changing which of
        /// the beats it hits. So the message is matched to the grid instead:
        ///
        ///   - too soon after the last one to be a different beat — it is that
        ///     beat again, and is dropped whole;
        ///   - a beat or two or five along the grid, within kSnapWindow — the
        ///     count moves by that many, so a dropped message costs nothing;
        ///   - nowhere near the grid — a stray, ignored, unless kRelockBeats of
        ///     them come in a row, which is the music having genuinely moved.
        ///
        /// All of which needs a grid worth matching against, so none of it
        /// happens until kSettleBeats have agreed with it, and a tap never goes
        /// through it at all: a person tapping a tempo in *is* a run of beats
        /// that do not fit the old grid, and does not double-send.
        void markBeat(double when, BeatSource source);

        /// Start the grid over at `when` — the downbeat is here, and this is
        /// beat one. A MIDI Start means this, and so does `midi align`.
        void restart(double when, BeatSource source);

        /// Beat number plus phase, as one continuous value: 12.25 is a quarter
        /// of the way through beat 12. Patterns trigger on the integer part
        /// changing and animate off the fraction.
        ///
        /// **The integer part is a count of beats.** It moves forward on every
        /// beat and never backwards, which is what a pattern watching for a
        /// change needs, and it moves by however many beats actually went by —
        /// two across a message that never arrived, none across one that
        /// arrived twice. That is what makes it safe to count groups off it:
        /// every second beat of this number is every second beat of the music.
        /// It was not always so, and the look that needed it counted the
        /// changes itself, which is worse — a counter running at frame rate
        /// sees a jump of two as one. See markBeat() and beatInBar().
        double beatPosition(double now) const;

        /// Beats since the downbeat the grid is counting from.
        long long beatsSinceDownbeat(double now) const;

        /// Which beat of the bar it is: 0 is the one, 3 is the four.
        ///
        /// Two assumptions live in here and both are worth saying out loud. The
        /// bar is four beats long, because the music this rig plays to is in
        /// four. And it starts at the last downbeat *declared* — a MIDI Start,
        /// or `midi align` — which for a source that never says (Mixxx sends
        /// beats and nothing at all about bars) is wherever the grid happened
        /// to begin.
        ///
        /// So the one can be wrong, by one, two or three beats, and there is
        /// exactly one gesture that fixes it: `midi align`, on the one. That is
        /// a much better deal than it sounds, because the alternative is not a
        /// correct bar — it is no bar at all. The looks that need one used to
        /// count beats off the wire for themselves, and a wire that duplicates
        /// and drops beats moved them onto a different beat of the bar every
        /// few minutes. A bar that is wrong until someone taps it beats one
        /// that is right on average and never twice in the same place.
        int beatInBar(double now) const;

        /// The beat the current bar counts from. Moved by restart().
        long long getDownbeat() const;

        /// Seconds since the beat we are in started.
        float timeSinceBeat(double now) const;

        /// How long one beat lasts. The tempo, said the way anything dividing
        /// or multiplying the beat actually needs it — 60/bpm computed from a
        /// float bpm that was itself computed from this is a round trip that
        /// costs precision for nothing.
        double beatSeconds() const;

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

        /// `bpm=128.0 src=midi_clock lock=yes beat=417 bar_beat=3`, for STATUS.
        std::string describe(double now) const;

    private:
        /// Phase is held just short of the next beat when free-run is off, so a
        /// stalled clock decays to a steady value instead of retriggering.
        static constexpr double kHeldPhase = 0.999;

        /// Folds one measured beat-to-beat gap into the tempo, if it could
        /// plausibly be one beat. Smoothed, never taken raw.
        void learnPeriod(double interval);

        /// What a filed beat does about free-run having predicted beats of its
        /// own since the last one we took.
        enum class Advance
        {
            Grid,  ///< never below the prediction, so the count only goes up
            Fresh, ///< past it, so a look watching for a change always sees one
            Exact, ///< what the beats say, prediction or no prediction
        };

        /// Files a beat as the `steps`-th one after the last we accepted, and
        /// puts the grid on it.
        void takeBeat(double when, long long steps, Advance advance = Advance::Grid);

        std::atomic<double> period{60.0 / 128.0};
        std::atomic<double> anchor{-1.0}; ///< when the current beat started
        std::atomic<long long> beatNumber{0};

        /// The last beat we *accepted*, and its number.
        ///
        /// Separate from the anchor and the beat number above, which setBpm()
        /// also writes: it re-seats both mid-beat to hold the phase across a
        /// tempo change, so measuring the gap between beats from them would
        /// measure the gap since the last tempo message instead — and a Mixxx
        /// stream sends one of those on every beat. Every judgement markBeat()
        /// makes is against these two.
        std::atomic<double> lastBeatAt{-1.0};
        std::atomic<long long> lastBeatCount{0};

        std::atomic<long long> downbeat{0}; ///< the beat the bar counts from
        std::atomic<int> settled{0};        ///< agreeing beats, up to kSettleBeats
        std::atomic<int> strayBeats{0};     ///< off-grid beats in a row

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


    /// Which loudness signal, of the several a mapping sends.
    ///
    /// They are not interchangeable and the difference is the whole reason this
    /// is an enum rather than one number. Mixxx sends all three; a look picks
    /// the one whose behaviour it wants, by name, so that no pattern has to
    /// know a MIDI note number.
    enum class VuSource
    {
        /// The instantaneous level, resent every 40ms. Peaks on every kick, so
        /// anything driven from it hits on transients — and anything meant to
        /// sit still will flash.
        Instant,

        /// The same level averaged over about two seconds: the loudness of the
        /// track rather than of the waveform. What a backdrop wants.
        Average,

        /// A meter bar — quantised, steppy, and useful for something that
        /// should move in visible increments rather than continuously.
        Meter,
    };

    /// How many there are, for sizing. Not a VuSource value itself.
    constexpr int kVuSourceCount = 3;

    const char* describeVuSource(VuSource source);


    /// How loud the music is, 0..1, on each of the sources above.
    ///
    /// Fed from the VU messages on the MIDI cable — the only thing on there
    /// that says anything about the *sound* as opposed to the grid.
    ///
    /// A reading is held flat until it is old enough to be suspect, then fades
    /// out. The fade is not an effect, it is a dead-man's switch: a level held
    /// at its last value forever would leave the rig sitting lit at whatever
    /// the music happened to be doing when the link dropped, which looks
    /// exactly like it is still working.
    ///
    /// Held flat first rather than decaying from the instant of the reading,
    /// because anything reading this every frame is reading it far more often
    /// than the meter updates. Decaying immediately would put a few percent of
    /// sag on every frame, varying with how long ago the last message landed —
    /// a ripple on something that is supposed to be steady.
    class AudioLevel
    {
    public:
        /// Seconds a reading stands unaltered. Comfortably longer than the gap
        /// between messages from a meter that is working.
        static constexpr double kHoldFor = 0.35;

        /// Seconds after which a stale reading has fallen to zero.
        static constexpr double kStaleAfter = 0.8;

        /// A new reading, 0..1, taken at `when`.
        void set(VuSource source, float level, double when);

        /// The level as of `now`, with the hold and staleness decay applied.
        float get(VuSource source, double now) const;

        /// Whether that source has been heard from recently enough to trust.
        bool isLive(VuSource source, double now) const;

        /// Readings taken, for status and for telling "silent" apart from
        /// "not wired up".
        unsigned long long getUpdates(VuSource source) const;

        /// True when any source is reporting.
        bool isAnyLive(double now) const;

        /// `avg=0.42 inst=0.61 meter=0.33`, for status.
        std::string describe(double now) const;

    private:
        struct Reading
        {
            std::atomic<float> level{0.0f};
            std::atomic<double> stamp{-1.0};
            std::atomic<unsigned long long> updates{0};
        };

        Reading readings[kVuSourceCount];
    };

    /// The process's level meter. Same reasoning as sharedBeatClock().
    AudioLevel& sharedAudioLevel();
}
