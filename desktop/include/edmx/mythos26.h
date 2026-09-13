// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>

#include "lib/eanim/automation_curve.h"
#include "lib/eanim/generator_hsv.h"
#include "lib/ecore/hsv.h"

#include "relics/scanner/generic_patterns.h"
#include "relics/scanner/scanner_patterns.h"

#include "edmx/beat_clock.h"
#include "edmx/beat_trigger.h"
#include "edmx/state_machine.h"

///
/// mythos26 — the show.
///
/// Unlike `jacket` and the `obelisk_*` looks, these are not a relic's patterns
/// borrowed for a rig. They are written for the rig, which means two things
/// are different and both are on purpose:
///
///   - **The coordinate space is the stage.** A relic look has to be
///     stretched into the physical space it was tuned in; these are written
///     against the scanner's stage - the obelisk at x 0..7 and y 0..42, the
///     ring on it, the truss beside it - which is the room as the environment
///     files place it, and the space the generic looks the show borrows were
///     written in. See scanner::kStageTop and the note on the frame in
///     makeMythos26StateMachine.
///   - **They can read the music.** A relic has no idea what the track is
///     doing. This rig does: the beat over the trigger rack, and the analysis
///     over the audio bus — see edmx/beat_clock.h.
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
    /// **It does not decide when it hits.** It binds to one of the rig's shared
    /// triggers — `rate` is which one — and fires its envelope on the frame that
    /// trigger says a hit landed, at the sub-frame offset the trigger reports.
    /// Everything about *when* lives in edmx::TriggerRack, and the reason it
    /// lives there rather than here is that four looks each working it out
    /// privately is four looks that drift apart. See beat_trigger.h.
    class Pattern_Mythos_BeatPulse : public eanim::GeneratorHSV
    {
    public:
        Pattern_Mythos_BeatPulse();

        void init();

        /// The look has been entered — the machine is blending toward it, or it
        /// was made active outright.
        ///
        /// Asks this look's trigger for a hit, if `entry_hit` is set. Called by
        /// the state wrapper rather than by init(), because entering is a thing
        /// that happens every visit and init() happens once, when the machine
        /// is built and nobody is looking.
        void onEnter();

        /// Whether entering asks for a hit. See bHitOnEntry.
        void setHitOnEntry(bool bHit) { bHitOnEntry = bHit; }
        bool getHitOnEntry() const { return bHitOnEntry; }

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

        /// The four rates, which are also the four triggers — see
        /// edmx::triggerNameForRate(). Kept spelled here as well because a look
        /// reads them as its own vocabulary, and they were here first.
        static constexpr float kQuarterTime{edmx::kQuarterTime};
        static constexpr float kHalfTime{edmx::kHalfTime};
        static constexpr float kOnBeat{edmx::kOnBeat};
        static constexpr float kDoubleTime{edmx::kDoubleTime};

        /// Hits per beat: 0.25 once a bar, 0.5 half time, 1 on the beat, 2
        /// double time.
        ///
        /// Snapped to one of those four, because the useful values are the
        /// musical ones and everything between them is a rig drifting against
        /// the track.
        ///
        /// It divides the beat *count*, never the tempo: the point of half time
        /// is the same hit, half as often, and stretching the envelope with the
        /// rate would soften it instead. So double time keeps the envelope it
        /// had, which at the default shape means the fall no longer finishes
        /// between hits and the rig hovers rather than pulses; shorten `decay`
        /// if that is not the look. Half and quarter time give the fall room it
        /// did not have and show the envelope's real shape, often for the first
        /// time.
        float getPulseRate() const { return pulseRate; }

        /// Sets the rate, snapped to one of the four.
        ///
        /// Where the slow rates *land* is not set here and is not this look's
        /// to decide: half time takes the one and the three of the bar, quarter
        /// time takes the one, and the bar is the clock's — see
        /// BeatClock::beatInBar(). That is a change from the version of this
        /// that seated its own pairs off the beat you set the rate on, and the
        /// reason for it is that a per-look seat is not a bar. It could not be
        /// shared, so two looks in half time could sit on opposite beats; it
        /// had to be re-made on every cue change; and it was counted off beat
        /// messages, which Mixxx duplicates and drops, so it wandered on its
        /// own between gestures. One bar for the whole rig, wrong until someone
        /// hits `midi align` on the one, is the better trade.
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
        /// Where the hits come from. The clock is not held here any more:
        /// this look no longer asks where the beat is, only whether its
        /// trigger fired.
        TriggerRack* triggers{nullptr};

        /// What setEnvelope() was last given. Kept only so the numbers can be
        /// read back; the curve is what actually runs.
        float attackSeconds{0.15f};
        float decaySeconds{0.600f};

        /// Which trigger this look is bound to, as hits per beat. Snapped to
        /// one of the four; see setPulseRate().
        float pulseRate{kOnBeat};

        /// Whether entering this look asks its trigger to fire.
        ///
        /// True is what a cue wants: coming up dark for up to a bar reads as a
        /// cue that did not come up. False is what a light with a job of its
        /// own wants — the UV told to flash should join the grid the rig is
        /// already on, not start a new one under the operator's thumb.
        ///
        /// Either way the *trigger* fires, not this look privately, so a cue
        /// coming up lit brings everything on that rate up with it.
        bool bHitOnEntry{true};

        /// Set by onEnter(), spent on the next tick. Banked rather than acted
        /// on because entry happens between frames and the rack is ticked at
        /// the top of one; the ask has to be made from inside tick() to land.
        bool entryPending{false};

        float level{0.0f};
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


    /// One flat colour, at one level. The simplest look there is.
    ///
    /// It exists to be driven rather than to be watched: with `level` on a
    /// modulation and `color` set per layer, it is the additive hit layers -
    /// red on the ring off `bass_hits`, green on the obelisk off `mid_hits`,
    /// blue on the truss off `high_hits`. The look itself knows nothing about
    /// audio; it is a colour and a number, and the bus turns the number.
    ///
    /// Which is the point worth keeping. Nothing here reads a channel, so the
    /// same class is also the UV par's `off` / `on` and the `hits` cue's
    /// near-black base, and a fourth use tomorrow needs no fourth pattern.
    class Pattern_Mythos_Solid : public eanim::GeneratorHSV
    {
    public:
        /// White, and unsaturated, by default: the levelLook states that
        /// predate this knob - the UV par's `off` and `on` - never set it, and
        /// render exactly as they did before it existed.
        ecore::HSV color{0.0f, 0.0f, 1.0f};

        /// Scales the colour's own value rather than replacing it, so a dark
        /// swatch stays dark at full level. The same shape as vu_pulse's
        /// wash: the colour is a ceiling, the number is how much of it.
        float level{1.0f};

        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
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


    // ========================================================================
    // The show's own looks. See "pattern spec.txt" at the top of the checkout
    // for the list they were written from.
    //
    // Every one of these stands on the scanner's stage - the ring, the obelisk
    // and the truss in one (x, y) - rather than on a 0..1 line, because that
    // is the space the looks it sits beside (fire, the rain) were written in,
    // and a show that flew a wave down the stage in one cue and along a line
    // in the next would read as two rigs. So they are scanner::PatternScanners:
    // a resettable clock, nodeCoord() and stageAlpha() for free.
    //
    // And every one has an `intensity` knob, 0..1, because the surface has
    // three pads per cue - low, medium, high - and a pad has to mean the same
    // thing on every look. What it *scales* is the look's own business: the
    // depth of a wash, how hard a hit lands, how many drops are in the air.
    // What it never does is take the rig to black, so `low` is a quieter cue
    // and not a fault.
    // ========================================================================

    /// The base for a show look: the clock, and the intensity knob every cue
    /// answers to. The knob is declared once here so it is spelled the same on
    /// all of them; each look reads it in its own way.
    class Pattern_MythosLook : public scanner::PatternScanner
    {
    public:
        /// How much of the look is happening, 0..1. See the note above.
        float intensity{1.0f};

        virtual void reflect(ecore::PropertyBag& bag) override
        {
            bag.add("intensity", intensity, 0.0f, 1.0f);
        }
    };


    /// A field of noise between two colours, drifting over the stage.
    ///
    /// The simplest thing the show has, and two of its cues: cyan into deep
    /// blue for the neuron scene, pink into purple for the fire tunnel. The
    /// field is two octaves of value noise on the stage's coordinates, so
    /// the patches wander across the obelisk's faces and around the ring
    /// rather than marching up a strip.
    ///
    /// The two colours are knobs, so any pair is this look with a picker; the
    /// cues differ only in what they are built with.
    class Pattern_Mythos_NoiseWash : public Pattern_MythosLook
    {
    public:
        /// The two ends of the field: `color_a` where the noise is low,
        /// `color_b` where it is high.
        ecore::HSV colorA{185.0f, 0.9f, 1.0f};
        ecore::HSV colorB{230.0f, 1.0f, 0.4f};

        /// How fast the patches drift, and how big they are.
        float speed{1.0f};
        float scale{1.0f};

        /// The dimmest the field goes, 0..1: the wash is a backdrop and a
        /// backdrop that blacks out in patches reads as fixtures failing.
        float floorLevel{0.25f};

        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /// A colour whose level follows a channel of the audio bus, with a second
    /// colour that lands on the kick.
    ///
    /// The presence cues: red riding the mids for the geode, purple riding
    /// them for filter blown - and on the second, green on every kick. The
    /// wash never falls below `floor`, which is what the spec's "clamp to 0.2"
    /// means: a wash keyed to a track that has gone quiet is still a wash.
    ///
    /// It reads the bus itself rather than taking a `mod`, for the same
    /// reason the audio meter does: the channel is what the cue *is*, and a
    /// modulation declared beside the show would survive onto every cue after
    /// it. `hit` at 0 is no kick at all, which is how the geode is built.
    class Pattern_Mythos_BusWash : public Pattern_MythosLook
    {
    public:
        ecore::HSV color{0.0f, 1.0f, 1.0f};

        /// Which channel the wash follows. Set per cue, not a knob - see
        /// Pattern_Audio_Meter::channel for why a pad must not be able to
        /// point it elsewhere.
        AudioChannel channel{AudioChannel::MidPresence};

        /// The level the wash never drops below, and how far a full channel
        /// takes it above that.
        float floorLevel{0.2f};
        float gain{1.0f};

        /// Seconds to close most of a gap to a new reading. Presence is a slow
        /// number already, but the bus hands it over at whatever rate the
        /// binding fires; a touch of slew keeps the wash from stepping.
        float slew{0.15f};

        /// The kick: a second colour landing over the wash on `hit_channel`,
        /// `hit` being how much of it shows - 0 is none, and the wash alone.
        ecore::HSV hitColor{120.0f, 1.0f, 1.0f};
        float hit{0.0f};
        AudioChannel hitChannel{AudioChannel::BassHits};
        /// Seconds for a hit to fall away. The bus's hits channels are already
        /// transients; this holds the top of one long enough to be seen.
        float hitDecay{0.25f};

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// The wash's level and the hit's, 0..1, for tests.
        float getLevel() const { return level; }
        float getHitLevel() const { return hitLevel; }

    private:
        AudioLevel* bus{nullptr};
        float level{0.0f};
        float hitLevel{0.0f};
    };


    /// The whole rig snapping to a new colour on every kick.
    ///
    /// The glitch cue. Between kicks the rig holds the last colour at
    /// `floor`; a kick flashes it up and moves the hue on by a golden-ratio
    /// step, so consecutive colours are always far apart and the sequence
    /// never settles into a cycle. `scatter` breaks the colour up across the
    /// fixtures - each one a hashed nudge off the hue, re-rolled per kick -
    /// which is the glitch: not a wash changing colour, a rig re-dealt.
    ///
    /// A kick is a rising edge on the bass hits channel through `threshold`.
    /// One edge, one colour: a transient a few frames wide must not re-deal
    /// the rig on every frame it is above the line.
    class Pattern_Mythos_KickColor : public Pattern_MythosLook
    {
    public:
        AudioChannel channel{AudioChannel::BassHits};
        float threshold{0.45f};

        /// The level held between kicks, and seconds for the flash to fall
        /// back to it.
        float floorLevel{0.55f};
        float decay{0.35f};

        float saturation{1.0f};
        /// How far a fixture strays from the rig's hue, as a share of the
        /// wheel. 0 is one colour on everything.
        float scatter{0.12f};

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// A kick landed: what tick() does on the edge, callable directly so
        /// a test can deal the rig without a bus.
        void onKick();

        unsigned int getKicks() const { return kicks; }
        float getHue() const { return hue; }

    private:
        AudioLevel* bus{nullptr};
        bool wasAbove{false};
        unsigned int kicks{0};
        float hue{0.0f};
        float flash{0.0f};
    };


    /// Bands of canyon colour pouring down the stage, and a rainbow that
    /// takes the rig over on the beat.
    ///
    /// The palette is the fly-through: orange rock, brown, the green of the
    /// floor, the blue of the sky - laid over the stage's height and
    /// scrolling downward so the ring and the obelisk are passed through
    /// rather than lit. On every hit of `rate` the whole rig cross-fades to
    /// a rainbow spread over its height and back, on the same kind of
    /// envelope beat_pulse plays, off the same shared trigger.
    class Pattern_Mythos_CanyonWave : public Pattern_MythosLook
    {
    public:
        Pattern_Mythos_CanyonWave();

        /// Palette cycles per second past a point, downward.
        float speed{0.12f};
        /// How many times the palette repeats over the stage's height.
        float waves{1.0f};

        /// The rainbow's envelope: how fast it arrives and how long it stays.
        void setEnvelope(float attackSeconds, float decaySeconds);
        void setPulseRate(float pulsesPerBeat);
        float getPulseRate() const { return pulseRate; }

        eanim::AutomationCurveTrigger envelope;

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
        virtual void reflectCurves(eanim::CurveBag& bag) override;

        /// How much rainbow is showing right now, 0..1, for tests.
        float getRainbowLevel() const { return rainbow; }

    private:
        ecore::HSVPalette canyon;
        TriggerRack* triggers{nullptr};
        float attackSeconds{0.05f};
        float decaySeconds{0.45f};
        float pulseRate{edmx::kOnBeat};
        float rainbow{0.0f};
    };


    /// A gradient between two colours up the stage, strobing on the beat.
    ///
    /// The punk cue: purple into white, scrolling, and on every hit of
    /// `rate` the whole rig snaps to the bright end and falls back - a strobe
    /// on the grid rather than a free-running one, so it lands with the
    /// flash layer and the UV. Bright by design: the gradient's floor is
    /// high and the strobe goes to full white.
    class Pattern_Mythos_GradientStrobe : public Pattern_MythosLook
    {
    public:
        Pattern_Mythos_GradientStrobe();

        ecore::HSV colorA{280.0f, 0.95f, 1.0f};   // punk purple
        ecore::HSV colorB{0.0f, 0.0f, 1.0f};      // white
        /// What the strobe snaps to.
        ecore::HSV strobeColor{0.0f, 0.0f, 1.0f};

        /// Gradient cycles per second past a point, and how many fit the
        /// stage's height.
        float speed{0.5f};
        float waves{1.0f};

        void setEnvelope(float attackSeconds, float decaySeconds);
        void setPulseRate(float pulsesPerBeat);
        float getPulseRate() const { return pulseRate; }

        eanim::AutomationCurveTrigger envelope;

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
        virtual void reflectCurves(eanim::CurveBag& bag) override;

        float getStrobeLevel() const { return strobe; }

    private:
        TriggerRack* triggers{nullptr};
        float attackSeconds{0.0f};
        float decaySeconds{0.12f};
        float pulseRate{edmx::kOnBeat};
        float strobe{0.0f};
    };


    /// Fire 2012, as a show cue: the generic look with the intensity knob
    /// on the front, turning how eagerly the floor ignites.
    class Pattern_Mythos_Fire : public scanner::Pattern_Generic_Fire2012
    {
    public:
        float intensity{1.0f};

        void applyIntensity();

        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /// Matrix rain, as a show cue: every drop its own colour, the intensity
    /// knob turning how many are in the air - and already raining when the
    /// cue comes up. The generic look spawns its drops above the stage and
    /// lets them fall in, which from a pad is a second of black before the
    /// first head crosses the obelisk's top; a cue cannot open dark, so
    /// reset() rolls the storm forward before anyone sees it.
    class Pattern_Mythos_Rain : public scanner::Pattern_Generic_MatrixRain
    {
    public:
        Pattern_Mythos_Rain();

        float intensity{1.0f};

        void applyIntensity();

        virtual void reset() override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /// One hue after another, on every fixture at once.
    ///
    /// For the UV par, whose three channels are three banks of the same
    /// blacklight: a hue means nothing to it, but a hue *wheel* - one bank,
    /// then two, then one again as the wheel passes each secondary - is the
    /// blacklight breathing between a third and two thirds, never off, which
    /// is what the rain cue asks of it. On a colour fixture it is a plain
    /// rainbow with no spatial term.
    class Pattern_Mythos_HueCycle : public eanim::GeneratorHSV
    {
    public:
        /// Wheels per second.
        float speed{0.25f};
        float saturation{1.0f};
        float level{1.0f};

        void init() { phase = 0.0f; }

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float phase{0.0f};
    };


    /// The show's sixteen slots, in the order a UI shows them.
    ///
    /// Eight of them written, the rest placeholders waiting on the spec. The
    /// static pair and the beat flash that used to live here are cues on the
    /// generic machine and the audio bus. See makeGenericStateMachine and
    /// beatPulseState.
    std::unique_ptr<StateMachinePattern> makeMythos26StateMachine();

    /// A flash over the whole rig, for an additive layer: `off`, and `flash`
    /// - the beat pulse, white by default and a `color` knob away from
    /// anything else, its envelope drawable at the desk. What the spec calls
    /// the bpm flash layer: patched over the show at `blend: add`, so a cue
    /// that wants it turns it on and every other cue never sees it.
    std::unique_ptr<StateMachinePattern> makeFlashStateMachine();
}
