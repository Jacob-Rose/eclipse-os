// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/mythos26.h"

#include <algorithm>
#include <cmath>

#include "edmx/beat_clock.h"
#include "edmx/beat_trigger.h"

using namespace edmx;

// ============================================================================
// beat_pulse
// ============================================================================

Pattern_Mythos_BeatPulse::Pattern_Mythos_BeatPulse()
    : triggers(&sharedTriggerRack())
{
    // RestartHold rather than Restart, because the envelope does not fit
    // between two beats at any tempo this runs at. See the class comment.
    setHoldOnRetrigger(bHoldOnRetrigger);
    setEnvelope(attackSeconds, decaySeconds);
}

void Pattern_Mythos_BeatPulse::setEnvelope(float inAttackSeconds, float inDecaySeconds)
{
    attackSeconds = std::max(inAttackSeconds, 0.0f);
    decaySeconds  = std::max(inDecaySeconds, 0.001f);

    envelope.curve.clear();
    envelope.curve.addKey(0.0f, 0.0f);
    envelope.curve.addKey(attackSeconds, 1.0f, easing_functions::EaseOutCubic);
    envelope.curve.addKey(attackSeconds + decaySeconds, 0.0f);
}

void Pattern_Mythos_BeatPulse::setHoldOnRetrigger(bool bHold)
{
    bHoldOnRetrigger = bHold;
    envelope.retriggerMode = bHold ? eanim::RetriggerMode::RestartHold
                                   : eanim::RetriggerMode::Restart;
}

void Pattern_Mythos_BeatPulse::setPulseRate(float pulsesPerBeat)
{
    // Snapped, because a slider spanning 0.25..2 will hand over 1.37 on the
    // way past and 1.37 hits a beat is a rig drifting against the track. The
    // snap is the rack's, so that the rate this lands on and the trigger it
    // binds to cannot disagree.
    pulseRate = snapPulseRate(pulsesPerBeat);
}

void Pattern_Mythos_BeatPulse::reflect(ecore::PropertyBag& bag)
{
    bag.add("attack", attackSeconds, 0.0f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("decay", decaySeconds, 0.01f, 3.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("intensity", intensity, 0.0f, 1.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("hold", bHoldOnRetrigger, [this] { setHoldOnRetrigger(bHoldOnRetrigger); });

    // Snapped on the way in, so the slider lands on the four rates rather than
    // between them. Where the slow ones land is the clock's bar, not this
    // knob's - see setPulseRate(). It is also which shared trigger this look
    // fires off, so two looks on the same rate are on the same hits.
    bag.add("rate", pulseRate, kQuarterTime, kDoubleTime, [this] { setPulseRate(pulseRate); });

    // Whether coming up asks for a hit. On for a cue, which should not open
    // dark; off for a light joining a grid the rig is already running.
    bag.add("entry_hit", bHitOnEntry);

    bag.add("color", pulseColor);
}

void Pattern_Mythos_BeatPulse::reflectCurves(eanim::CurveBag& bag)
{
    // The envelope itself, drawable. The attack/decay knobs are the shorthand
    // that *writes* this curve, so the two can fight: a drawn shape holds
    // until either knob is touched, at which point setEnvelope rebuilds the
    // rise-and-fall over it. Last writer wins, which is the only honest
    // answer for two spellings of one shape.
    bag.add("envelope", envelope.curve);
}

void Pattern_Mythos_BeatPulse::init()
{
    level = 0.0f;
    envelope.reset();
    entryPending = false;
}

void Pattern_Mythos_BeatPulse::onEnter()
{
    // Banked rather than acted on: entry happens between frames, and the rack
    // is ticked at the top of one. tick() spends it.
    entryPending = bHitOnEntry;
}

void Pattern_Mythos_BeatPulse::tick(float deltaTime)
{
    // A cue coming up asks its trigger to fire, and is served on the next
    // rack tick - which is the top of the next frame, because the rack runs
    // before anything ticks. Asking the shared trigger rather than firing
    // privately is the point: the whole rate comes up together.
    if (entryPending)
    {
        entryPending = false;
        triggers->armEntry(triggerNameForRate(pulseRate));
    }

    // When a hit lands is not this look's decision. The rack made it once, for
    // the whole rig, before any of this ran - so two looks on the same rate
    // fire on the same frame with the same offset instead of each arriving at
    // an answer of their own. `sinceHit` is how long ago inside this frame, so
    // the impulse lands where it happened rather than on the frame boundary.
    const BeatTrigger& trigger = triggers->forRate(pulseRate);
    if (trigger.fired)
    {
        envelope.triggerAt(trigger.sinceHit);
    }

    // The trigger reads the curve's peak across the frame rather than its value
    // at the end of one, which is what keeps the envelope safe to shorten — see
    // eanim::AutomationCurve::peak.
    envelope.tick(deltaTime);

    // Intensity scales the envelope on its way into the level, so it takes the
    // hit down toward the floor rather than taking the whole look down: at 0
    // there is no flash, and whatever the floor is holding up is still there.
    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    const float hitLevel = std::clamp(envelope.getValue(), 0.0f, 1.0f)
                         * std::clamp(intensity, 0.0f, 1.0f);
    level = floorValue + ((1.0f - floorValue) * hitLevel);
}

void Pattern_Mythos_BeatPulse::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    (void)node; // the whole rig hits together; nothing here is spatial yet

    inOutColor = pulseColor;
    inOutColor.setBrightnessAlpha(pulseColor.getValFloat() * level);
}

// ============================================================================
// vu_pulse
// ============================================================================

Pattern_Mythos_VuPulse::Pattern_Mythos_VuPulse()
    : meter(&sharedAudioLevel())
{
    // The flash sits over a lit wash here, so it wants a shorter, sharper
    // envelope than beat_pulse's - see the cue list, which is where both
    // looks' opening shapes live.
}

void Pattern_Mythos_VuPulse::reflect(ecore::PropertyBag& bag)
{
    // The flash's own knobs first, under the names beat_pulse gives them, so
    // the same look tunes the same way whichever state you are in. `color` is
    // the flash's, and every `base_` below belongs to the wash.
    pulse.reflect(bag);

    bag.add("base_color", baseColor);
    bag.add("base_gain", baseGain, 0.0f, 2.0f);
    bag.add("base_floor", baseFloor, 0.0f, 1.0f);
    bag.add("base_smoothing", baseSmoothing, 0.0f, 2.0f);
}

void Pattern_Mythos_VuPulse::reflectCurves(eanim::CurveBag& bag)
{
    // the flash's envelope, same name as on beat_pulse - one shape, drawable
    // from either state
    pulse.reflectCurves(bag);
}

void Pattern_Mythos_VuPulse::init()
{
    pulse.init();
    baseLevel = 0.0f;
    mixLayers();
}

void Pattern_Mythos_VuPulse::tick(float deltaTime)
{
    pulse.tick(deltaTime);

    const double now = nowSeconds();

    // The floor only applies while the meter is actually reporting. Holding a
    // dim red up when the link is dead would be the rig lying about having a
    // signal, and it is the one state where "looks fine" is the wrong answer.
    const float floorValue = meter->isLive(baseSource, now)
        ? std::clamp(baseFloor, 0.0f, 1.0f)
        : 0.0f;
    const float target = std::clamp(
        floorValue + (meter->get(baseSource, now) * baseGain), 0.0f, 1.0f);

    if (baseSmoothing <= 0.0f || deltaTime <= 0.0f)
    {
        baseLevel = target;
    }
    else
    {
        // Symmetric one-pole, framed in seconds rather than as a per-frame
        // coefficient so the look does not change with the frame rate. Equally
        // slow in both directions on purpose: an asymmetric filter that snaps
        // upward keeps every transient it is supposed to be removing.
        const float rate = 1.0f - std::exp(-deltaTime / baseSmoothing);
        baseLevel += (target - baseLevel) * rate;
    }

    // Both layers are now where they are for this frame, so put them together
    // once - see mixLayers().
    mixLayers();
}

void Pattern_Mythos_VuPulse::mixLayers()
{
    const float flash = std::clamp(pulse.getLevel(), 0.0f, 1.0f);
    const ecore::HSV& flashColor = pulse.pulseColor;

    // Blend the two layers as *chroma vectors* — hue as an angle, saturation as
    // a radius — rather than as a hue and a saturation apiece.
    //
    // This is the whole composite, and it is worth understanding before
    // touching it, because the obvious version is wrong in two different ways
    // and this one is wrong in neither.
    //
    // Adding the flash to the wash gives pink for white over red. Lerping the
    // hue instead walks the long way round the wheel: blue over red goes
    // through *green*, a colour nobody put in the look. Both were tried here.
    //
    // Interpolating the chroma vector has neither failure, because it goes
    // through the middle of the wheel rather than around the rim. Two hues far
    // apart lose saturation on the way between them and pass through something
    // near white, which is what a flash washing a colour out actually looks
    // like — red to blue goes red, pale magenta, blue.
    //
    // And it *is* the desaturation this look always did, not a replacement for
    // it: white has no chroma at all, so the vector shrinks straight to the
    // origin, the hue never moves, and the wash fades to exactly white. That
    // case comes out of this arithmetic unchanged rather than being special
    // cased, which is the reason to prefer it over a branch on "is the flash
    // achromatic".
    const float baseAngle  = baseColor.getHueFloat() * 0.01745329252f;
    const float flashAngle = flashColor.getHueFloat() * 0.01745329252f;

    const float baseSat  = baseColor.getSatFloat();
    const float flashSat = flashColor.getSatFloat();

    const float x = (baseSat * std::cos(baseAngle))
                  + (((flashSat * std::cos(flashAngle)) - (baseSat * std::cos(baseAngle))) * flash);
    const float y = (baseSat * std::sin(baseAngle))
                  + (((flashSat * std::sin(flashAngle)) - (baseSat * std::sin(baseAngle))) * flash);

    const float saturation = std::min(std::sqrt((x * x) + (y * y)), 1.0f);

    // Hold the wash's hue when there is no chroma left to take one from. At
    // that saturation nothing on the rig can tell, but a hue that jumps to
    // whatever atan2(0, 0) returns would show the moment it came back.
    float hue = baseColor.getHueFloat();
    if (saturation > 0.0005f)
    {
        hue = std::atan2(y, x) * 57.2957795131f;
        if (hue < 0.0f)
        {
            hue += 360.0f;
        }
    }

    mixColor = ecore::HSV(hue, saturation, 1.0f);

    // Each layer's own value is its ceiling, so a picked colour that is dark is
    // dark on the rig: the meter scales the wash's, the envelope the flash's.
    mixLevel = std::max(baseLevel * baseColor.getValFloat(),
                        flash * flashColor.getValFloat());
}

void Pattern_Mythos_VuPulse::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    (void)node;

    // Mixed once in tick() rather than here. The whole rig is one colour, and
    // this runs per fixture per frame - 356 of them on mythos26 - so the two
    // trig calls and the square root would otherwise be paid 356 times for one
    // answer.
    inOutColor = mixColor;
    inOutColor.setBrightnessAlpha(mixLevel);
}

// ============================================================================
// tv static
// ============================================================================

namespace
{
    /// Cheap integer avalanche (the murmur3 finaliser's cousin). Any change in
    /// the input changes about half the output bits, which is the only property
    /// static needs: neighbouring fixtures on consecutive frames must not
    /// resemble each other.
    unsigned int hashBits(unsigned int x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }
}

void Pattern_Mythos_TvStatic::reflect(ecore::PropertyBag& bag)
{
    bag.add("monochrome", monochrome);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
}

void Pattern_Mythos_TvStatic::init()
{
    frame = 0;
}

void Pattern_Mythos_TvStatic::tick(float deltaTime)
{
    (void)deltaTime; // one step per rendered frame, not per unit of time
    ++frame;
}

void Pattern_Mythos_TvStatic::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const unsigned int index = (node != nullptr)
        ? static_cast<unsigned int>(node->getStripIdx())
        : 0u;

    // Mix the two into one hash rather than hashing them separately: the frame
    // advances by one and the index advances by one, so anything less than a
    // proper mix leaves visible diagonal structure marching across the rig.
    const unsigned int bits = hashBits((frame * 2654435761u) ^ (index * 2246822519u));

    const float a = static_cast<float>(bits & 0xFFFFu) / 65535.0f;
    const float b = static_cast<float>((bits >> 16) & 0xFFFFu) / 65535.0f;

    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    const float value = floorValue + ((1.0f - floorValue) * a);

    inOutColor = monochrome
        ? ecore::HSV(0.0f, 0.0f, 1.0f)
        : ecore::HSV(b * 360.0f, 1.0f, 1.0f);
    inOutColor.setBrightnessAlpha(value);
}

// ============================================================================
// placeholders
// ============================================================================

void Pattern_Mythos_Placeholder::reflect(ecore::PropertyBag& bag)
{
    bag.add("hue", hue, 0.0f, 360.0f);
}

void Pattern_Mythos_Placeholder::init()
{
    phase = 0.0f;
}

void Pattern_Mythos_Placeholder::tick(float deltaTime)
{
    // ~9 seconds a cycle. Slow enough to read as "nothing is happening here
    // yet" rather than as a look someone meant.
    phase += deltaTime * 0.11f;
    if (phase > 1.0f)
    {
        phase -= 1.0f;
    }
}

void Pattern_Mythos_Placeholder::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    float position = 0.0f;
    if (node != nullptr && node->GetStripNodeType() == eio::StripNodeType::MAPPED2D)
    {
        // mythos26's frame runs 0..1 along the rig on y, so the coordinate is
        // the position — no scaling, unlike a relic look.
        position = static_cast<eio::HSVStripNode_Mapped2D*>(node)->coord.y;
    }

    const float breath = 0.5f + (0.5f * std::sin((phase + (position * 0.25f)) * 6.2831853f));

    inOutColor = ecore::HSV(hue, 0.7f, 1.0f);
    inOutColor.setBrightnessAlpha(0.10f + (0.15f * breath));
}

// ============================================================================
// the show
// ============================================================================

namespace
{
    /// One look in the table. Same shape as the relic version in
    /// state_machine.cpp: default-construct, init, hand back as a generator.
    ///
    /// Anything a look needs configuring is set on the instance afterwards
    /// rather than passed to a constructor — see `placeholder`. Keeping every
    /// entry built identically is what lets the table below read as a list.
    ///
    /// Both of these spell out the return type and return the derived pointer
    /// directly, rather than ending on a static_pointer_cast the way the relic
    /// table does. The cast leaves a derived-typed temporary to destroy, and
    /// gcc 16 devirtualises that destructor across the two generator types in
    /// this file, then warns that one of them overruns the other's allocation.
    /// It is a false positive — the vtable picks the right one at runtime — but
    /// letting the conversion happen at the return is both cleaner and quiet.
    template <typename PatternT>
    StateDef look(const char* name)
    {
        StateDef def;
        def.name = name;
        def.make = []() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<PatternT>();
            pattern->init();
            return pattern;
        };
        return def;
    }

    /// A look that wants to know it has been entered.
    ///
    /// The same shape as the scanner's State_ScannerHSV and for the same
    /// reason: entering is not something a generator can see. A beat look uses
    /// it to ask its trigger for a hit, so a cue comes up lit instead of dark
    /// for up to a bar - and asks the *shared* trigger, so everything on that
    /// rate comes up with it.
    template <typename PatternT>
    class State_BeatHSV : public State_GenericHSV
    {
    public:
        State_BeatHSV(const char* stateName, eio::RelicIO* io,
                      std::shared_ptr<PatternT> inPattern)
            : State_GenericHSV(stateName, io)
            , beatPattern(inPattern)
        {
            setGenerator(inPattern);
        }

    protected:
        virtual void onStateChangeState(StateStatus inStatus) override
        {
            // Entered fresh: either the machine is blending toward us, or we
            // were set active directly from Off. Becoming Active at the end of
            // a blend also lands here as Active, but from TransitionIn - asking
            // for a second hit then would fire twice on one cue change.
            const bool bEntering = inStatus == StateStatus::TransitionIn
                || (inStatus == StateStatus::Active && GetStatus() == StateStatus::Off);

            State_GenericHSV::onStateChangeState(inStatus);

            if (bEntering && beatPattern)
            {
                beatPattern->onEnter();
            }
        }

        std::shared_ptr<PatternT> beatPattern;
    };

    /// A look that fires on the beat, with the shape of its hit and how often.
    ///
    /// Attack and decay are what a beat look *is* - a crack and a trail, or a
    /// swell and a long fall - so they belong in the cue list beside the name
    /// rather than buried in a constructor, and the rate is beside them because
    /// a look that opens in half time is a different cue, not a mistuned one.
    /// All three stay live knobs once it is running; these are what it opens on.
    ///
    /// The lambda is where the concrete type is known, which is what keeps the
    /// machine itself from needing to know about any of them.
    template <typename PatternT>
    StateDef beatLook(const char* name, float attackSeconds, float decaySeconds, float pulseRate,
                      bool bHitOnEntry = true)
    {
        StateDef def;
        def.name = name;
        def.make = [attackSeconds, decaySeconds, pulseRate, bHitOnEntry]()
            -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<PatternT>();
            pattern->setEnvelope(attackSeconds, decaySeconds);
            pattern->setPulseRate(pulseRate);
            pattern->setHitOnEntry(bHitOnEntry);
            pattern->init();
            return pattern;
        };
        def.makeState = [](const char* stateName, eio::RelicIO* io,
                           std::shared_ptr<eanim::GeneratorHSV> generator) {
            return std::static_pointer_cast<State_GenericHSV>(
                std::make_shared<State_BeatHSV<PatternT>>(
                    stateName, io, std::static_pointer_cast<PatternT>(generator)));
        };
        return def;
    }

    /// Static, in greys or in colour.
    StateDef staticLook(const char* name, bool monochrome)
    {
        StateDef def;
        def.name = name;
        def.make = [monochrome]() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<Pattern_Mythos_TvStatic>();
            pattern->monochrome = monochrome;
            pattern->init();
            return pattern;
        };
        return def;
    }

    /// An empty slot, tinted so the states are told apart on the rig.
    StateDef placeholder(const char* name, float hue)
    {
        StateDef def;
        def.name = name;
        def.make = [hue]() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<Pattern_Mythos_Placeholder>();
            pattern->hue = hue;
            pattern->init();
            return pattern;
        };
        return def;
    }

    /// One flat level of white: a light held where it is put. The UV par's
    /// `off` and `on` are this at 0 and 1; `level` is a knob so `on` can be
    /// trimmed at the desk without becoming a different state.
    class Pattern_Level : public eanim::GeneratorHSV
    {
    public:
        float level{1.0f};

        virtual void render(eio::HSVStripNode* /*node*/, ecore::HSV& inOutColor) const override
        {
            inOutColor = ecore::HSV(0.0f, 0.0f, level);
        }

        virtual void reflect(ecore::PropertyBag& bag) override
        {
            bag.add("level", level, 0.0f, 1.0f);
        }
    };

    StateDef levelLook(const char* name, float level)
    {
        StateDef def;
        def.name = name;
        def.make = [level]() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<Pattern_Level>();
            pattern->level = level;
            return pattern;
        };
        return def;
    }
}

std::unique_ptr<StateMachinePattern> edmx::makeUvStateMachine()
{
    // The UV par's three modes, as a layer's machine. `flash` is the show's
    // beat pulse - the same envelope, drawable at the desk, the same rate
    // knob - on a light that is nothing but a level, so the UV hits on the
    // beat while the truss around it follows the scanner.
    //
    // It does not hit on entry, which is the one place its cue list differs
    // from the show's. A cue coming up wants to be seen immediately; the UV is
    // joining a grid the rig is already running, and a hit at the instant the
    // operator pressed the button is a flash off the beat - which is precisely
    // what made it look out of step with the truss. It waits, and lands with
    // everything else on the `beat` trigger.
    std::vector<StateDef> states = {
        levelLook("off", 0.0f),
        //                                     attack decay  rate  entry hit
        beatLook<Pattern_Mythos_BeatPulse>("flash", 0.02f, 0.30f, 1.0f, false),
        levelLook("on", 1.0f),
    };

    CoordFrame frame;
    // a short cross-fade: a light changing mode should snap, not swim
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "uv", std::move(states), 0, frame, 0.15f));
}

std::unique_ptr<StateMachinePattern> edmx::makeMythos26StateMachine()
{
    // ------------------------------------------------------------------
    // The show, one line per state, in the order a UI shows them.
    //
    // Writing a look means adding a GeneratorHSV to mythos26.h/.cpp and
    // changing its line here; nothing else in the runner, the protocol or the
    // UI needs to know. The remaining slot_* are placeholders.
    //
    // The first two numbers on a beatLook are its envelope: how long the hit
    // takes to reach full, and how long it takes to fall back. They are the
    // difference between a crack and a swell. The third is its rate: 0.5 half
    // time, 1 on the beat, 2 double time. All three are live knobs at the desk
    // once it is running - these are what the cue opens on.
    //
    // Both open on the beat. Half time is a rate rather than a return of the
    // beat divider that used to live here: that offered 4 as well, and the
    // clock counts beats with no idea which of them is the one, so "on 4" fired
    // at the right rate on an arbitrary beat of the bar with no usable way to
    // move it. A pair has a usable way - setting the rate, or entering the cue,
    // seats it on the beat you did that on - which is exactly what a bar's
    // worth of beats did not.
    //
    // Renaming a state means renaming it in three places: here,
    // MYTHOS26_STATES in python/eclipse_dmx/config.py, and the button table in
    // python/eclipse_dmx/viewer.py. The executable is the authority; the other
    // two are so a config can be validated and a button can be labelled
    // without one running.
    // ------------------------------------------------------------------
    std::vector<StateDef> states = {
        //                                          attack  decay  rate
        beatLook<Pattern_Mythos_BeatPulse>("beat_pulse", 0.15f, 0.60f, 1.0f),
        beatLook<Pattern_Mythos_VuPulse>  ("vu_pulse",   0.10f, 0.45f, 1.0f),

        staticLook("tv_static_mono", true),
        staticLook("tv_static", false),

        placeholder("slot_5", 210.0f),
        placeholder("slot_6", 275.0f),
        placeholder("slot_7", 320.0f),
    };

    // ------------------------------------------------------------------
    // The rig's own space, not a relic's.
    //
    // A jacket look needs the monowire's coordinates and an obelisk look needs
    // its 8 x 43 field, because both were tuned against a physical object. These
    // were written here, so the sane space is the simple one: x is flat and y
    // runs 0..1 from the first fixture to the last.
    // ------------------------------------------------------------------
    CoordFrame rig;
    rig.originX = 0.0f;
    rig.spanX   = 0.0f;
    rig.originY = 0.0f;
    rig.spanY   = 1.0f;

    // Shorter than the jacket's 0.4s. These are cues in a show rather than
    // moods on a garment, and a beat-locked look wants to arrive promptly.
    const float transitionTime = 0.25f;

    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "mythos26", std::move(states),
        /*segmentId*/ 0, // nothing here branches on segment; only relic looks do
        rig,
        transitionTime));
}
