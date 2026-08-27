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
    /// It is retriggered by polling the clock once a frame rather than by a
    /// callback from the MIDI thread: the clock predicts between beats, so
    /// polling is both simpler and immune to a beat that lands mid-render. What
    /// it watches for is the clock's beat number *changing*, not what it
    /// changed to — see the note on counting in tick().
    class Pattern_Mythos_BeatPulse : public eanim::GeneratorHSV
    {
    public:
        Pattern_Mythos_BeatPulse();

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
        virtual void reflectCurves(eanim::CurveBag& bag) override;

        /// White to open on. The whole look is one colour and one envelope, so
        /// this is the knob that changes it — a `color` property, which means a
        /// swatch at the desk rather than three sliders spelling out an HSV.
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

        /// The three rates. Named rather than left as loose numbers because
        /// 1.37 hits per beat is not a thing anyone wants and a slider will
        /// otherwise hand you one.
        static constexpr float kHalfTime{0.5f};
        static constexpr float kOnBeat{1.0f};
        static constexpr float kDoubleTime{2.0f};

        /// Hits per beat: 0.5 half time, 1 on the beat, 2 double time.
        ///
        /// Snapped to one of those three, because the useful values are the
        /// musical ones and everything between them is a rig drifting against
        /// the track. Set it through setPulseRate(), which is also what re-seats
        /// half time — see below.
        ///
        /// It divides the beat *count*, never the tempo: the point of half time
        /// is the same hit, half as often, and stretching the envelope with the
        /// rate would soften it instead. So double time keeps the envelope it
        /// had, which at the default shape means the fall no longer finishes
        /// between hits and the rig hovers rather than pulses; shorten `decay`
        /// if that is not the look. Half time gives the fall room it did not
        /// have and shows the envelope's real shape, often for the first time.
        float getPulseRate() const { return pulseRate; }

        /// Sets the rate and re-seats half time on the nearest beat.
        ///
        /// The re-seat is the whole reason a divider was taken out of here once
        /// and half time is back. The clock counts beats and has no idea which
        /// of them is the one, so anything slower than the beat has to land on
        /// an arbitrary member of the group — that is what sank `beat div 4`,
        /// which fired at the right rate in the wrong place with no usable way
        /// to move it. Two beats is the one case where there is a usable way:
        /// the pair re-seats from wherever you set it, so hitting half time (or
        /// hitting the cue) on the beat you want puts it there. That is a
        /// gesture someone can make mid-set; hunting for the top of a bar four
        /// beats wide was not.
        ///
        /// Only half time can tell: on the beat and double time land on the
        /// same instants whatever beat they are counted from.
        void setPulseRate(float pulsesPerBeat);

        /// RestartHold when set, Restart when not — the `hold` knob.
        ///
        /// Kept as a bool of its own rather than reflected off retriggerMode
        /// because the tuning surface is floats and bools, and these are the
        /// only two of the three modes that make sense for a single envelope on
        /// a beat. Set it through setHoldOnRetrigger() so the mode follows.
        bool bHoldOnRetrigger{true};
        void setHoldOnRetrigger(bool bHold);

        /// How hard the hit lands, 0..1. Full by default.
        ///
        /// Scales the envelope, not the whole output: at a lifted `floor` this
        /// brings the hit down toward the level between hits rather than
        /// dimming the rig, so 0 is "no flash" and not "no light".
        ///
        /// It is a knob of its own rather than the flash colour's brightness —
        /// which would do the same arithmetic — because the two are wanted at
        /// different moments. The colour is a look; this is how much of the
        /// track the look is taking, and on vu_pulse it is what buys the wash
        /// room under the flash. Changing it should not mean opening a picker.
        float intensity{1.0f};

        /// Level held between pulses, 0..1. Zero is a hard blackout between
        /// beats; lift it if the rig needs to stay visible.
        float floorLevel{0.0f};

        /// The level the envelope is at right now, 0..1. Exposed for tests and
        /// for anything that wants to show the beat on screen.
        float getLevel() const { return level; }

        /// The envelope `seconds` after a beat, 0..1.
        float envelopeAt(float seconds) const { return envelope.curve.evaluate(seconds); }
        /// Its largest value across a span, which is what a frame should show
        /// when the rise is short enough to crest inside one. See tick().
        float envelopePeak(float from, float to) const { return envelope.curve.peak(from, to); }

    private:
        /// Whether the beat we have just arrived at is one this rate hits on.
        bool isHitBeat() const;

        /// Puts half time's pairs on the nearest beat. See setPulseRate().
        void seatHalfTime();

        BeatClock* clock{nullptr};

        /// What setEnvelope() was last given. Kept only so the numbers can be
        /// read back; the curve is what actually runs.
        float attackSeconds{0.15f};
        float decaySeconds{0.600f};

        float pulseRate{kOnBeat};

        /// Beats this look has seen, and the one half time counts its pairs
        /// from.
        ///
        /// Counted here rather than taken from the clock's beat number, which
        /// is guaranteed to move forward on a beat but *not* to move by one —
        /// see tick(). Every second beat has to be every second beat that
        /// actually happened, or it picks a different member of the pair every
        /// time the number jumps.
        long long beatsSeen{0};
        long long seatBeat{0};

        /// Double time's mid-beat hit, once per beat. Cleared by the beat.
        bool offbeatFired{false};

        /// Beat we last saw. Starts unset so the first tick hits rather than
        /// waiting up to a whole beat to show anything.
        long long lastBeat{0};
        bool started{false};

        float level{0.0f};
    };


    /// The beat in white over the loudness in red.
    ///
    /// Two layers doing different jobs. The lower one is continuous and follows
    /// the VU meter, so the rig has a floor that breathes with the music
    /// instead of going black between hits. The upper one is the same envelope
    /// as beat_pulse on top of it.
    ///
    /// Both colours are knobs — `base_color` for the wash and `color` for the
    /// flash, the same name beat_pulse gives its own — so red under white is
    /// what it opens on rather than what it is. The `base_` prefix is how the
    /// pair reads: everything the wash owns carries it, and everything the
    /// flash owns is named the way beat_pulse names it.
    ///
    /// They are composited by blending *chroma vectors* — through the middle of
    /// the colour wheel rather than around its rim. For the white flash this
    /// opens on that is exactly the desaturation it has always done: at full
    /// flash the red has become white, where adding white to red would have
    /// given you pink. For a flash with a colour of its own it is what stops
    /// blue over red going through green on the way. See mixLayers(), which is
    /// where both of those failures are written down.
    class Pattern_Mythos_VuPulse : public eanim::GeneratorHSV
    {
    public:
        Pattern_Mythos_VuPulse();

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
        virtual void reflectCurves(eanim::CurveBag& bag) override;

        /// The flash. Same shape, colour and rate knobs as beat_pulse.
        Pattern_Mythos_BeatPulse pulse;

        /// The layer underneath: a backdrop that follows how loud the track is.
        ///
        /// Red to open on, and a `base_color` swatch at the desk. Its own value
        /// is a ceiling on the wash, so picking a dark colour gives a dark
        /// wash — the meter scales it rather than replacing it.
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

        /// The flash's envelope and rate, forwarded, so a cue list can shape a
        /// vu_pulse the same way it shapes a beat_pulse. See beatLook in
        /// mythos26.cpp.
        void setEnvelope(float attackSeconds, float decaySeconds)
        {
            pulse.setEnvelope(attackSeconds, decaySeconds);
        }

        void setPulseRate(float pulsesPerBeat) { pulse.setPulseRate(pulsesPerBeat); }

        /// The wash level right now, for tests.
        float getBaseLevel() const { return baseLevel; }

        /// The colour the two layers came to this frame, for tests.
        const ecore::HSV& getMixColor() const { return mixColor; }

    private:
        /// Puts the wash and the flash together into one colour and one level.
        ///
        /// Done once a frame in tick() rather than per fixture in render(),
        /// because the whole rig is one colour here and render() is called for
        /// every one of them.
        void mixLayers();

        AudioLevel* meter{nullptr};
        float baseLevel{0.0f};

        ecore::HSV mixColor{0.0f, 1.0f, 1.0f};
        float mixLevel{0.0f};
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
