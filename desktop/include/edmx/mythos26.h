// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>

#include "lib/eanim/automation_curve.h"
#include "lib/eanim/generator_hsv.h"
#include "lib/ecore/hsv.h"

#include "edmx/beat_clock.h"
#include "edmx/state_machine.h"

///
/// mythos26 — the show.
///
/// Unlike `jacket` and the `obelisk_*` looks, these are not a relic's patterns
/// borrowed for a rig. They are written for the rig, which means two things
/// are different and both are on purpose:
///
///   - **The coordinate space is the rig itself.** A relic look has to be
///     stretched into the physical space it was tuned in; these are written
///     against 0..1 along the rig, so `coord.y` is just "how far along".
///   - **They can read the beat.** A relic has no idea what the music is doing.
///     This rig does, over MIDI — see edmx/beat_clock.h.
///
/// They are still plain `GeneratorHSV`s, so they run through the same state
/// machine, with the same cross-fades, as everything else. Nothing here is a
/// special case in the show runner.
///

namespace edmx
{

    /// The whole rig swelling on the beat.
    ///
    /// A slow rise into a long fall — a swell rather than a crack. It was the
    /// other way round first, 12ms up and 200ms down, which read as the rig
    /// *hitting* the beat and left a clear dark gap before the next one; that
    /// pair of numbers is the whole difference if it ever wants to hit again.
    ///
    /// One thing to know before retuning it: the envelope is now longer than a
    /// beat at any danceable tempo — a beat is 469ms at 128bpm — so at `beat
    /// div 1` the fall never finishes before the next beat cuts it short. That
    /// is why it runs in `RetriggerMode::RestartHold`: the new pass restarts the
    /// timeline, but the output is held at the level it had reached until the
    /// rise climbs back past it, so the rig swells between a trough and full
    /// rather than stepping to dark on every beat. `Restart` is the old
    /// behaviour and is the right one for a curve that fits inside the gap.
    ///
    /// The envelope is an eanim::AutomationCurveTrigger, and the beat is the
    /// impulse that fires it. Nothing about the shape lives here any more: it is
    /// three keys on `envelope.curve`, and any other shape is the same three
    /// calls with different numbers.
    ///
    /// It is retriggered by the beat *number* changing rather than by a callback
    /// from the MIDI thread: the clock predicts between beats, so polling it
    /// once a frame is both simpler and immune to a beat that lands mid-render.
    class Pattern_Mythos_BeatPulse : public eanim::GeneratorHSV
    {
    public:
        Pattern_Mythos_BeatPulse();

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// White for now. The whole look is one colour and one envelope, so
        /// this is the knob that changes it.
        ecore::HSV pulseColor{0.0f, 0.0f, 1.0f};

        /// The envelope, and the timeline the beat plays it on.
        ///
        /// Reach straight into `envelope.curve` for a shape that is not a rise
        /// and a fall, or `envelope.retriggerMode` for what a beat landing
        /// mid-pass should do. setEnvelope() is the shorthand for the common
        /// case.
        eanim::AutomationCurveTrigger envelope;

        /// Rebuilds the curve as a rise to full and a fall back to dark.
        ///
        /// The rise is linear and the fall is EaseOutCubic — quick off the top
        /// and then trailing, which is what a light doing this actually looks
        /// like; a linear fall reads as a fade rather than as a decay.
        void setEnvelope(float attackSeconds, float decaySeconds);

        float getAttackSeconds() const { return attackSeconds; }
        float getDecaySeconds() const { return decaySeconds; }

        /// RestartHold when set, Restart when not — the `hold` knob.
        ///
        /// Kept as a bool of its own rather than reflected off retriggerMode
        /// because the tuning surface is floats and bools, and these are the
        /// only two of the three modes that make sense for a single envelope on
        /// a beat. Set it through setHoldOnRetrigger() so the mode follows.
        bool bHoldOnRetrigger{true};
        void setHoldOnRetrigger(bool bHold);

        /// Level held between pulses, 0..1. Zero is a hard blackout between
        /// beats; lift it if the rig needs to stay visible.
        float floorLevel{0.0f};

        /// Beats between hits: 1 on every beat, 2 on every other, 4 once a bar.
        ///
        /// Which beat of the pair or the bar it lands on is whichever one was
        /// current when the count started — the clock counts beats, not bars,
        /// because nothing upstream reliably says where a bar begins. `beat`
        /// (a tap) re-seats it, which is how you move it onto the one.
        void setBeatsPerPulse(int beats);
        int getBeatsPerPulse() const { return beatsPerPulse; }

        /// The level the envelope is at right now, 0..1. Exposed for tests and
        /// for anything that wants to show the beat on screen.
        float getLevel() const { return level; }

        /// The envelope `seconds` after a beat, 0..1.
        float envelopeAt(float seconds) const { return envelope.curve.evaluate(seconds); }
        /// Its largest value across a span, which is what a frame should show
        /// when the rise is short enough to crest inside one. See tick().
        float envelopePeak(float from, float to) const { return envelope.curve.peak(from, to); }

    private:
        BeatClock* clock{nullptr};

        int beatsPerPulse{1};

        /// What setEnvelope() was last given. Kept only so the numbers can be
        /// read back; the curve is what actually runs.
        float attackSeconds{0.15f};
        float decaySeconds{0.600f};

        /// Pulse we last fired on. Starts unset so the first tick pulses rather
        /// than waiting up to a whole beat to show anything.
        long long lastBeat{0};
        bool started{false};

        float level{0.0f};
    };


    /// The beat in white over the loudness in red.
    ///
    /// Two layers doing different jobs. The red one is continuous and follows
    /// the VU meter, so the rig has a floor that breathes with the music
    /// instead of going black between hits. The white one is the same envelope
    /// as beat_pulse on top of it, defaulting to every second beat — on a busy
    /// track, hitting every beat and being lit underneath at the same time is
    /// too much light and the hits stop reading as hits.
    ///
    /// They are composited by desaturating rather than adding: at full flash
    /// the red has become white, which is what "a white flash over red" looks
    /// like, where adding white to red would give you pink.
    class Pattern_Mythos_VuPulse : public eanim::GeneratorHSV
    {
    public:
        Pattern_Mythos_VuPulse();

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// The flash. Same shape as beat_pulse, on twos by default.
        Pattern_Mythos_BeatPulse pulse;

        /// The layer underneath: a backdrop that follows how loud the track is.
        ecore::HSV baseColor{0.0f, 1.0f, 1.0f}; ///< red

        /// Which loudness signal drives it. The two-second average, because a
        /// backdrop should hold still — VuSource::Instant peaks on every kick
        /// and turns the wash into a second pulse. Switchable so a look that
        /// *wants* that can have it.
        VuSource baseSource{VuSource::Average};

        /// What a full-scale meter reading maps to. 1 tracks the level
        /// literally; lower it to leave the flash more headroom above the wash.
        float baseGain{1.0f};

        /// Lifts the wash off black while the meter is live. Zero by default,
        /// so silence really is dark.
        float baseFloor{0.0f};

        /// Seconds for the backdrop to close most of a gap to a new reading.
        /// Zero follows the meter exactly.
        ///
        /// This has been wrong twice, so it is worth writing down what it is
        /// for. It is *not* VU ballistics — an asymmetric fast-attack slow-
        /// release, which is how a meter is drawn, keeps every transient on the
        /// way up and so still flashes on each kick. It is a symmetric
        /// low-pass: equally slow in both directions, which is what actually
        /// removes beat-rate pulsing and leaves the shape of the track.
        ///
        /// Modest by default because the meter this reads is already averaged
        /// upstream (see midi.vu_note). Raise it if the source is the
        /// instantaneous level instead.
        float baseSmoothing{0.25f};

        void setBeatsPerPulse(int beats) { pulse.setBeatsPerPulse(beats); }
        int getBeatsPerPulse() const { return pulse.getBeatsPerPulse(); }

        /// The wash level right now, for tests.
        float getBaseLevel() const { return baseLevel; }

    private:
        AudioLevel* meter{nullptr};
        float baseLevel{0.0f};
    };


    /// Every fixture a new random value, every frame.
    ///
    /// No smoothing, no motion, no beat — deliberately. The look is the
    /// absence of correlation: the moment consecutive frames relate to each
    /// other it stops reading as static and starts reading as a bad pattern.
    ///
    /// The values come from hashing (frame number, fixture index) rather than
    /// from a random generator with state. That keeps render() const and
    /// stateless while still giving every fixture its own value every frame,
    /// and it means a given frame is reproducible, which is the difference
    /// between a testable look and one you can only eyeball.
    class Pattern_Mythos_TvStatic : public eanim::GeneratorHSV
    {
    public:
        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// True for greys, false for random hues.
        bool monochrome{false};

        /// Lowest brightness a fixture can draw. Zero lets fixtures go fully
        /// dark, which is what makes it read as static rather than as a
        /// shimmer.
        float floorLevel{0.0f};

    private:
        unsigned int frame{0};
    };


    /// A placeholder look, so a slot in the show can be switched to and seen
    /// before it has been written.
    ///
    /// Deliberately plain — a dim, slow breath on one hue. It is not trying to
    /// be good; it is trying to make it obvious that the state changed and that
    /// this slot is still empty. Replace one by writing a real GeneratorHSV and
    /// swapping the line in makeMythos26StateMachine().
    class Pattern_Mythos_Placeholder : public eanim::GeneratorHSV
    {
    public:
        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// Degrees. Set after construction, so every look in the table is built
        /// the same way — see the note on `look` in mythos26.cpp.
        float hue{0.0f};

    private:
        float phase{0.0f};
    };


    /// The show's seven states, in the order a UI shows them.
    std::unique_ptr<StateMachinePattern> makeMythos26StateMachine();
}
