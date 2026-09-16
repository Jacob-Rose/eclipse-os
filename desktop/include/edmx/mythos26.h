// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    // And every one has a `mode` - 1, 2 or 3, one pad on the surface
    // stepping through them - because a cue is not one look but the two or
    // three the scene behind it asks for at different moments: the geode
    // gone blue, the rain with its video off. What a mode *is* is the cue's
    // own, written beside it in the table; see ShowModes for the mechanism.
    //
    // They also keep an `intensity` knob, 0..1, scaling the look's own
    // measure of how much is happening - the depth of a wash, how hard a hit
    // lands, how many drops are in the air - and never taking the rig to
    // black. It used to be the surface's three pads; it is a knob for a
    // fader or a mod now, and the pads are the mode.
    // ========================================================================

    /// The mode every show cue answers to: 1, 2 or 3, and a pad that steps
    /// through them.
    ///
    /// Where the three intensity pads used to be. A number 0..1 that meant
    /// the same on every cue could not mean much on any of them - `low` was
    /// a quieter geode, when what the geode wanted was to go blue. So a mode
    /// is the cue's own: modes 2 and 3 are written beside the cue in
    /// makeMythos26StateMachine, a line each, and this is the rig-side half
    /// of the mechanism. The other half - a video off, a layer moved - is the
    /// cue table's `modes` in config/mythos-show.json, fired by the same pad.
    ///
    /// Mode 1 is the cue as built. The knobs it opened with are snapshotted
    /// once its setup has run, and every mode change puts all of them back
    /// before its own variant runs over them - so 2 is a diff on the cue and
    /// not on whatever 3 left behind, and 1 is the way back. Entering a look
    /// that was left in another mode puts it in 1; entering one already in 1
    /// touches nothing, so a knob tuned at the desk survives a cue change the
    /// way it always has, and a mode does not.
    ///
    /// Three modes is the floor, not the count: a cue with more variants
    /// listed beside it has more - the churn and the nova have four, three
    /// palettes and a rainbow - and the pad steps round however many the
    /// cue has. The knob's maximum says which, so the desk need not know.
    ///
    /// A mixin rather than a base, because the fire and the rain are generic
    /// looks with a base of their own.
    class ShowModes
    {
    public:
        /// The fewest modes any cue has. A cue with no variants still has
        /// three, so the pad does the same thing on every cue.
        static constexpr int kModeCount = 3;

        /// 1..modeCount(). A float because a knob is one; snapped on the way in.
        float mode{1.0f};

        /// How many modes this look has: kModeCount, or one more than its
        /// variants when there are more of those.
        int modeCount() const { return std::max(kModeCount, static_cast<int>(variants.size()) + 1); }

        /// `mode`, on the bag. First on every look, so it sits at the top of
        /// the knob pane on all of them.
        void reflectMode(ecore::PropertyBag& bag);

        /// Binds to the look this is part of, takes the snapshot, and keeps
        /// the variants for modes 2 and up in order. Called once the cue's
        /// setup has run - see showLook - so the snapshot is the cue and not
        /// the class. A mode past the end of the list is mode 1's values and
        /// nothing else.
        void initModes(eanim::GeneratorHSV& look, std::vector<std::function<void()>> variants);

        /// Knobs a mode change leaves where they are - out of the snapshot,
        /// so no mode puts them back. For a knob that is flown by hand
        /// rather than part of the look: the clouds' height and speed,
        /// which a pad sets and the flight glides to, and which a palette
        /// change has no business resetting. Call before initModes.
        void keepAcrossModes(std::initializer_list<const char*> names);

        /// Snaps `mode`, restores the snapshot, runs the variant.
        void applyMode();

        /// What entry does: back to mode 1 if the look was left elsewhere.
        void resetMode();

        int getMode() const { return static_cast<int>(mode); }

    private:
        eanim::GeneratorHSV* look{nullptr};
        std::vector<std::function<void()>> variants;
        std::vector<std::string> kept;
        std::vector<std::pair<std::string, float>> baseValues;
        std::vector<std::pair<std::string, ecore::HSV>> baseColors;
    };


    /// The base for a show look: the clock, the mode, and the intensity knob.
    /// Both knobs are declared once here so they are spelled the same on all
    /// of them; each look reads intensity in its own way.
    class Pattern_MythosLook : public scanner::PatternScanner, public ShowModes
    {
    public:
        /// How much of the look is happening, 0..1. See the note above.
        float intensity{1.0f};

        virtual void reset() override
        {
            PatternScanner::reset();
            resetMode();
        }

        virtual void reflect(ecore::PropertyBag& bag) override
        {
            reflectMode(bag);
            bag.add("intensity", intensity, 0.0f, 1.0f);
        }
    };


    /// A flat colour over the whole rig at a level - the house lights.
    ///
    /// The two idle cues either side of the show: white at half for the
    /// room before and after, and the blackout. A show look rather than a
    /// Pattern_Mythos_Solid so it has the mode and intensity knobs every
    /// cue on the pad has; the mode pad puts it back where it was, and the
    /// intensity fader takes it down.
    class Pattern_Mythos_House : public Pattern_MythosLook
    {
    public:
        ecore::HSV color{0.0f, 0.0f, 1.0f};
        /// How much of the colour, 0..1. 0 is the blackout.
        float level{0.5f};

        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
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
    /// cues differ only in what they are built with. `hue_cycle` turns the
    /// pair through the wheel together - the churn's rainbow mode: the same
    /// field, its two colours a fixed distance apart on a wheel that turns.
    ///
    /// The probe is painted `color_a` outright, so an eclipse scene keyed to
    /// the rig is keyed to the palette and not to a passing patch.
    class Pattern_Mythos_NoiseWash : public Pattern_MythosLook
    {
    public:
        /// The two ends of the field: `color_a` where the noise is low,
        /// `color_b` where it is high.
        ecore::HSV colorA{185.0f, 0.9f, 1.0f};
        ecore::HSV colorB{230.0f, 1.0f, 0.4f};

        /// Wheels per second the pair is turned through; 0 holds them.
        float hueCycle{0.0f};

        /// How fast the patches drift, and how big they are.
        float speed{1.0f};
        float scale{1.0f};

        /// The dimmest the field goes, 0..1: the wash is a backdrop and a
        /// backdrop that blacks out in patches reads as fixtures failing.
        float floorLevel{0.25f};

        /// A channel of the bus the field's level rides, and how much of the
        /// ride shows: at `follow` 0 the wash drifts on its own, which is the
        /// neuron cue; at 1 its brightness above the floor is the channel's,
        /// slewed. The tunnel follows Mixxx's average - the cable's own
        /// presence, on the bus whatever else is filling it - because a cue
        /// between two that move with the track and one that does not reads
        /// as the rig losing the music. The channel is set per cue, not a
        /// knob - see Pattern_Mythos_BusWash::channel.
        AudioChannel channel{AudioChannel::MidPresence};
        float follow{0.0f};
        float gain{1.0f};
        float slew{0.15f};

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// The channel's slewed level, 0..1, for tests.
        float getLevel() const { return level; }

        /// The pair as the wheel has turned them, for tests.
        ecore::HSV colorANow() const { return turned(colorA); }
        ecore::HSV colorBNow() const { return turned(colorB); }

    private:
        ecore::HSV turned(const ecore::HSV& color) const;

        AudioLevel* bus{nullptr};
        float level{0.0f};
        float hueOffset{0.0f};
    };


    /// A colour whose level follows a channel of the audio bus, with a second
    /// colour that lands on the kick.
    ///
    /// The presence cues: red riding Mixxx's instant VU for the geode,
    /// purple riding the mids for filter blown - and on the second, green
    /// on every kick. The
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

        /// What shape the hit is. At 0 the hit colour lands on the kick and
        /// fades. At 1 the kick is a pop of the wash's own colour to full,
        /// and the hit colour is what it leaves behind: as the pop falls the
        /// rig swings to it and then back to the wash. Blown is the second -
        /// pink, popping pink, glowing green after - which is what the scene
        /// does, and which a green landing *on* the kick did not read as.
        float afterglow{0.0f};

        /// A fine grain over the wash, 0..1: value noise at a scale of a
        /// fixture or two, drifting, eating into the level where it is low.
        /// The geode's blue mode, which wanted a small noise rather than a
        /// flat blue; 0 is a flat wash and the default.
        float texture{0.0f};

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
    /// A kick is a rising edge on `channel` through `threshold` - the bass
    /// hits by default; the show's glitch cue points it at the beat, which is
    /// the signal the Glitch scene re-deals on. One edge, one colour: a
    /// transient a few frames wide must not re-deal the rig on every frame
    /// it is above the line.
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
    /// turns the rig on the beat.
    ///
    /// The palette is the fly-through: orange rock, brown, the green of the
    /// floor, the blue of the sky - laid over the stage's height and
    /// scrolling downward so the ring and the obelisk are passed through
    /// rather than lit. The bands blend round the short side of the wheel,
    /// so what lies between two of them is a colour and not a grey.
    ///
    /// On every hit of `rate` the canyon's colours turn toward a rainbow
    /// spread over the rig's height and back - the hue swings, the
    /// saturation fills, and each band keeps its own level, lifted by
    /// `rainbow_lift` so the dark ones are seen to turn. A colour pulse,
    /// not a light pulse: the rig does not get brighter on the beat, which
    /// is what kept this from reading as a strobe. The envelope is the
    /// same shape beat_pulse plays, held over a beat that lands mid-fall,
    /// off the same shared trigger.
    class Pattern_Mythos_CanyonWave : public Pattern_MythosLook
    {
    public:
        Pattern_Mythos_CanyonWave();

        /// Palette cycles per second past a point, downward.
        float speed{0.12f};
        /// How many times the palette repeats over the stage's height. Half:
        /// three of the six bands on the rig at once, each spanning a good
        /// stretch of pillar and a few pars, so what is seen is the blend
        /// between them. At one the six were seven rows each on the
        /// obelisk and under two pars each on the truss - stripes.
        float waves{0.5f};
        /// How far a dark band comes up under the rainbow, 0..1: 0 keeps
        /// the canyon's levels exactly, 1 takes everything to full.
        float rainbowLift{0.35f};

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
        TriggerRack* triggers{nullptr};
        /// A swell rather than a snap: 0.05s up was the strobe.
        float attackSeconds{0.12f};
        float decaySeconds{0.45f};
        float pulseRate{edmx::kOnBeat};
        float rainbow{0.0f};
    };


    /// A gradient between two colours up the stage, dropping to a third on
    /// the music.
    ///
    /// The punk cue: punk purple into honey orange - Milk, Honey, Smoke,
    /// Bile's own two - scrolling slowly, and the whole rig dropping toward
    /// a navy dark blue and coming back. A strobe *down*, because that is
    /// what the scene's `smoke` does: it inverts lightness on the beat, so
    /// the visual goes dark where a flash layer would go white. Bright by
    /// design between the drops: the gradient's floor is high.
    ///
    /// What drives the drop is `follow`. At 1 - the cue - it is a channel
    /// of the bus, Mixxx's average: the rig sits as dark as the meter says,
    /// slewed, so the drop is the level of the track and not a grid's
    /// guess at it. At 0 it is the beat: on every hit of `rate` the
    /// envelope fires and the rig drops and comes back, on the grid so it
    /// lands with the UV on the kick - the half- and double-time modes.
    ///
    /// The scroll is slow on purpose. At half a cycle a second the whole
    /// rig went purple, white, purple every two seconds - a second pulse
    /// under the strobe, on no beat at all, which is what made the strobe
    /// feel sporadic.
    class Pattern_Mythos_GradientStrobe : public Pattern_MythosLook
    {
    public:
        Pattern_Mythos_GradientStrobe();

        ecore::HSV colorA{280.0f, 0.95f, 1.0f};   // punk purple
        ecore::HSV colorB{32.0f, 0.90f, 1.0f};    // honey orange
        /// What the strobe drops to.
        ecore::HSV strobeColor{228.0f, 1.0f, 0.22f};   // navy

        /// Gradient cycles per second past a point, and how many fit the
        /// stage's height.
        float speed{0.12f};
        float waves{1.0f};

        /// How much of the drop is the channel's rather than the beat's,
        /// 0..1, and the channel: Mixxx's average, gained and slewed. The
        /// slew is short - the meter arrives every 40ms and the drop should
        /// follow it, not smear it.
        float follow{1.0f};
        AudioChannel channel{AudioChannel::LevelAverage};
        float gain{1.0f};
        float slew{0.05f};

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
        /// The channel's slewed level, 0..1, for tests.
        float getLevel() const { return level; }

    private:
        TriggerRack* triggers{nullptr};
        AudioLevel* bus{nullptr};
        float level{0.0f};
        float attackSeconds{0.0f};
        /// Long enough to be seen on every beat: 0.12s was three frames,
        /// and a beat that fell between them was a beat the rig missed.
        float decaySeconds{0.22f};
        float pulseRate{edmx::kOnBeat};
        float strobe{0.0f};
    };


    /// Where the truss reads a stage simulation: as a row across the
    /// obelisk's width at this height, not as the line beside it that the
    /// environment places it on.
    ///
    /// The pars stand at x 10, climbing the obelisk's height. The flames are
    /// born at x 0..7 and reach three units either side, and the rain falls
    /// at x 0..7 with a radius under one - so the truss saw the ember bed on
    /// its lowest par and nothing of the rain at all. Reading it as a row is
    /// what the pars should have been doing for these two looks: ten lamps
    /// showing the fifth run from the floor, where the flames are still
    /// full and the drops are still passing. A knob, because which run is
    /// something to see on the rig.
    ///
    /// Only these two: a top-down cue - the canyon, the rain's own fall -
    /// does want the pars climbing beside the pillar, and they keep to it.
    constexpr float kTrussRowDefault = 4.0f;

    /// The stage point a truss node reads a simulation at: `u` along the
    /// truss spread over the obelisk's runs, at `row`. Null for anything that
    /// is not the truss.
    bool trussRowCoord(const eio::HSVStripNode* node, float row, ecore::Coordinate& outAt);


    /// Fire 2012, as a show cue: the generic look with the mode and the
    /// intensity knob on the front, the latter turning how eagerly the floor
    /// ignites, and the truss read as a row through the flames.
    class Pattern_Mythos_Fire : public scanner::Pattern_Generic_Fire2012, public ShowModes
    {
    public:
        float intensity{1.0f};
        float trussRow{kTrussRowDefault};

        void applyIntensity();

        virtual void reset() override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /// Matrix rain, as a show cue: every drop its own colour, the intensity
    /// knob turning how many are in the air - and already raining when the
    /// cue comes up. The generic look spawns its drops above the stage and
    /// lets them fall in, which from a pad is a second of black before the
    /// first head crosses the obelisk's top; a cue cannot open dark, so
    /// reset() rolls the storm forward before anyone sees it.
    ///
    /// The truss is read as a row through the storm - see kTrussRowDefault.
    ///
    /// Its colour glides. A mode that turns the rain green - or white - is
    /// a change to every drop in the air at once, and snapping it read as a
    /// cut where the cue table promised a mode. So `hue_spread` and `white`
    /// are *targets* here: the knob a mode or a desk sets, with the live
    /// value the drops render at easing toward it over `blend` seconds. A
    /// fresh cue lands on its targets at once; only a change after that
    /// glides.
    class Pattern_Mythos_Rain : public scanner::Pattern_Generic_MatrixRain, public ShowModes
    {
    public:
        Pattern_Mythos_Rain();

        /// The show's storm holds three times a relic's: the downpour modes
        /// ask for 80 drops, and the ceiling on the knob is the pool.
        static constexpr int kDropPool = 96;

        float intensity{1.0f};
        float trussRow{kTrussRowDefault};

        /// Where the colour is going - see the class comment. The generic
        /// look's `hueSpread` is where it is.
        float targetHueSpread{1.0f};
        /// 0 is the drops' own colour, 1 is white rain: the saturation is
        /// scaled down by it, head and tail alike.
        float white{0.0f};
        float targetWhite{0.0f};
        /// seconds for a colour change to arrive
        float blendSeconds{1.0f};

        void applyIntensity();

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /// Galaxies on a wash over a dark ground - the nova cue.
    ///
    /// Eclipse Nova paints Woitzel's Nova in the rig's colour: the galaxies
    /// are `rig_color`, the wash behind them a second colour, and the two
    /// sit on a background the scene's `base_amount` sets between black and
    /// the wash. This is the same three roles on the stage: `base` under
    /// everything, `galaxy` where a slow, large field of noise peaks - and
    /// swelling with the bass presence, which is what brightens the scene's
    /// galaxies - and `wash` in a finer field between them, at `wash_amount`.
    /// The probe is painted `galaxy`, so the scene's galaxies are the rig's.
    ///
    /// `hue_cycle` turns all three through the wheel together, for the
    /// rainbow mode; the base keeps its darkness, so the ground stays a
    /// ground at every hue.
    class Pattern_Mythos_Nova : public Pattern_MythosLook
    {
    public:
        ecore::HSV base{49.0f, 0.85f, 0.35f};       // the ground: the wash, dimmed - see the cue's base_amount
        ecore::HSV galaxy{182.0f, 0.90f, 1.0f};     // cyan
        ecore::HSV wash{49.0f, 0.85f, 1.0f};        // yellow: the scene's (1.00, 0.85, 0.15)

        /// How much of the wash shows between the galaxies, 0..1.
        float washAmount{0.55f};
        /// How fast the fields drift, and how big the galaxies are.
        float speed{1.0f};
        float scale{1.0f};
        /// Wheels per second, for the rainbow; 0 holds the palette.
        float hueCycle{0.0f};

        /// The galaxies swell with this channel, slewed; `follow` is how
        /// much of the swell shows.
        AudioChannel channel{AudioChannel::BassPresence};
        float follow{0.6f};
        float gain{1.0f};
        float slew{0.2f};

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// The channel's slewed level, 0..1, for tests.
        float getLevel() const { return level; }

    private:
        ecore::HSV turned(const ecore::HSV& color) const;

        AudioLevel* bus{nullptr};
        float level{0.0f};
        float hueOffset{0.0f};
    };


    /// Flying through a sunset over a floor of cloud - the cloud cue.
    ///
    /// The sky is a gradient down the stage's height, `sky_high` at the top
    /// into `sky_low` at the horizon; below the horizon the cloud deck,
    /// `cloud` with a slow field of noise streaming down the stage at
    /// `speed` so the rig is flown through rather than lit. `height` is
    /// where the horizon sits, 0..1 up the stage: fly low and the deck fills
    /// the rig, climb and it falls away to sky.
    ///
    /// Both `height` and `speed` are targets: the look glides to them over
    /// `glide` seconds rather than snapping - a pad or a mode sets the
    /// target, the flight takes its time. The modes are the heights: low,
    /// where the cue opens, then cruising, then high - so the mode pad is
    /// a climb and the numbered pads name an altitude outright. The speed
    /// is the cue's two pads, and a mode leaves it be. The scene's own
    /// height and speed are ramped from the desk the same way; see the
    /// cue in config/mythos-show.json.
    ///
    /// `clouds` is how much cloud there is: the share of the deck lit as
    /// tops catching the sunset, and wisps of the same streaming through
    /// the sky above the horizon - so a low flight is mostly cloud and a
    /// high one still has some.
    class Pattern_Mythos_CloudFlight : public Pattern_MythosLook
    {
    public:
        Pattern_Mythos_CloudFlight();

        ecore::HSV skyHigh{48.0f, 0.85f, 1.0f};     // the yellow overhead
        ecore::HSV skyLow{330.0f, 0.70f, 1.0f};     // the pink at the horizon
        ecore::HSV cloud{270.0f, 0.60f, 0.16f};     // the deck, in shadow

        /// Where the horizon is, 0..1 up the stage - the target. Opens low:
        /// the deck over three quarters of the rig.
        float height{0.25f};
        /// How fast the deck streams past, in stage heights a second - the target.
        float speed{0.25f};
        /// Seconds a change of either takes to arrive.
        float glide{2.0f};
        /// How much of the deck is lit from above, 0..1: the tops of the
        /// clouds catching the sunset.
        float glow{0.45f};
        /// How much cloud, 0..1: the tops on the deck and the wisps in the sky.
        float clouds{0.6f};

        void init();

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// Where the flight actually is, for tests.
        float getHeightNow() const { return heightNow; }
        float getSpeedNow() const { return speedNow; }
        /// How far through the deck's loop, 0..1, for tests.
        float getTravelled() const { return travelled; }

    private:
        float heightNow{0.25f};
        float speedNow{0.25f};
        /// The deck's place in its loop, 0..1. The deck is a field periodic
        /// in y - see valueNoiseLoop - so 1 is 0 and the wrap is not seen.
        /// It was an open field wrapped at 1, which put a jump in the deck
        /// every sixteen seconds at the cue's speed and every six at the
        /// pad's fast one: the clouds visibly resetting mid-flight.
        float travelled{0.0f};
    };


    /// The rainbow when the track is there, white noise when it is not -
    /// the reaction cue.
    ///
    /// Two looks and the presence deciding between them. Underneath, a low
    /// white grain: a fine field drifting over the stage at `floor`, never
    /// more than that, which is what an empty room sounds like on the rig.
    /// Over it, the rainbow: the wheel spread across the stage - `span`
    /// wheels from one side to the other, so the pars step through it one
    /// hue apart - and turning at `hue_rate`. How much of the rainbow shows
    /// is the presence above `threshold`, over a `knee` that spans most of
    /// the meter - so the presence *blends* the rainbow in, through the
    /// white, rather than switching it on at a level - and slewed longer
    /// than the other cues that read the bus, so a presence that jumps is a
    /// rainbow that swells. The probe is painted the rainbow's colour at
    /// the centre.
    class Pattern_Mythos_Reaction : public Pattern_MythosLook
    {
    public:
        /// The white grain, when nothing is playing.
        float floorLevel{0.30f};
        float grain{0.6f};

        /// The rainbow: wheels across the stage, and wheels a second.
        float span{0.6f};
        float hueRate{0.04f};

        /// Where the presence has to get to before the rainbow shows, and
        /// how far above that it takes to show all of it. Nearly the whole
        /// meter: this was 0.3 and 0.35, which put the entire blend inside a
        /// third of the range and read as a switch.
        float threshold{0.08f};
        float knee{0.72f};

        AudioChannel channel{AudioChannel::Presence};
        float gain{1.0f};
        float slew{0.60f};

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// The slewed presence and how much rainbow it buys, 0..1, for tests.
        float getLevel() const { return level; }
        float getRainbow() const;

    private:
        AudioLevel* bus{nullptr};
        float level{0.0f};
    };


    /// Mostly dark, with red-orange and a cyan blue emerging on the beat
    /// and hiding again before the next - the scaffold cue.
    ///
    /// Two things on a ground that is nearly black. The `glint`: a cyan
    /// blue where a slow, fine field peaks, the rig's own cold light - a
    /// few nodes at a time, drifting, the way the scene's struts catch it.
    /// The `ember`: red-orange in patches of a second field. Both come and
    /// go with the grid: on every hit of `rate` the envelope fires, the
    /// ember's patches open to `ember_spread` and the glint comes up, and
    /// over the fall they close and the rig goes back to its ground - the
    /// pattern emerging and hiding in time, which is what the spec asks
    /// for instead of a white flash over a look that was there anyway. So
    /// no flash layer on this cue, and the envelope is the same shape
    /// beat_pulse plays, off the same shared trigger, so it lands with
    /// everything else on the rate. The probe is painted the glint.
    class Pattern_Mythos_Scaffold : public Pattern_MythosLook
    {
    public:
        Pattern_Mythos_Scaffold();

        ecore::HSV ground{200.0f, 0.90f, 0.05f};
        ecore::HSV glint{190.0f, 0.85f, 0.75f};
        ecore::HSV ember{18.0f, 0.95f, 1.00f};

        /// How much of the field peaks as glint at the top of the beat: the
        /// share of the range, from the top. 0 is none.
        float glintAmount{0.30f};

        /// How far the ember's patches open at the top of the beat, 0..1.
        float emberSpread{0.8f};

        void setEnvelope(float attackSeconds, float decaySeconds);
        void setPulseRate(float pulsesPerBeat);
        float getPulseRate() const { return pulseRate; }

        eanim::AutomationCurveTrigger envelope;

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
        virtual void reflectCurves(eanim::CurveBag& bag) override;

        /// How far the pattern is out right now, 0..1, for tests.
        float getPulseLevel() const { return pulse; }

    private:
        TriggerRack* triggers{nullptr};
        /// Quick out, and gone before the next beat at a club tempo: the
        /// pattern is there on the hit and hidden by the time the next one
        /// lands, so each beat is seen to bring it back.
        float attackSeconds{0.06f};
        float decaySeconds{0.38f};
        float pulseRate{edmx::kOnBeat};
        float pulse{0.0f};
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


    /// The show's sixteen cues, in the order a UI shows them: the fourteen
    /// looks and the two house states either side of them. The
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
