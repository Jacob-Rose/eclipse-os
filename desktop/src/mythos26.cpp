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

    /// A light held where it is put, in whatever colour it is given.
    ///
    /// The UV par's `off` and `on` are this at 0 and 1, and slot_5's `hits`
    /// base is it at 0.03. `level` is a knob so `on` can be trimmed at the
    /// desk without becoming a different state - and so a mod can turn it,
    /// which is what makes the same class the three additive hit layers.
    StateDef levelLook(const char* name, float level)
    {
        StateDef def;
        def.name = name;
        def.make = [level]() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<Pattern_Mythos_Solid>();
            pattern->level = level;
            return pattern;
        };
        return def;
    }
}

// ============================================================================
// solid
// ============================================================================

void Pattern_Mythos_Solid::render(eio::HSVStripNode* /*node*/, ecore::HSV& inOutColor) const
{
    inOutColor = color;
    inOutColor.setBrightnessAlpha(color.getValFloat() * level);
}

void Pattern_Mythos_Solid::reflect(ecore::PropertyBag& bag)
{
    bag.add("color", color);
    bag.add("level", level, 0.0f, 1.0f);
}

StateDef edmx::beatPulseState()
{
    // The same numbers the show's cue opened on - a 0.15s rise into a 0.60s
    // fall, on the beat - because the look is unchanged; only the list holding
    // it is. See the note on beatLook for what the three of them are.
    return beatLook<Pattern_Mythos_BeatPulse>("beat_pulse", 0.15f, 0.60f, 1.0f);
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
    // Twelve empty slots, which is the size of the surface's page rather than
    // a guess: the show is being written from scratch, and a slot that exists
    // is a cue that can be switched to, mapped to a button and seen on the rig
    // before there is a look in it. The looks that were here have gone to the
    // generic list - see makeGenericStateMachine - because a look with no beat
    // and no show around it belongs in the free-standing pile.
    //
    // Writing one means adding a GeneratorHSV to mythos26.h/.cpp and changing
    // its line here; nothing else in the runner, the protocol or the UI needs
    // to know. The hues are only so the empty slots are told apart on the rig.
    //
    // Renaming a state means renaming it in three places: here,
    // MYTHOS26_STATES in python/eclipse_dmx/config.py, and the button table in
    // python/eclipse_dmx/viewer.py. The executable is the authority; the other
    // two are so a config can be validated and a button can be labelled
    // without one running.
    // ------------------------------------------------------------------
    std::vector<StateDef> states = {
        placeholder("slot_1", 0.0f),
        placeholder("slot_2", 30.0f),
        placeholder("slot_3", 60.0f),
        placeholder("slot_4", 90.0f),
        placeholder("slot_5", 120.0f),
        placeholder("slot_6", 150.0f),
        placeholder("slot_7", 180.0f),
        placeholder("slot_8", 210.0f),
        placeholder("slot_9", 240.0f),
        placeholder("slot_10", 270.0f),
        placeholder("slot_11", 300.0f),
        placeholder("slot_12", 330.0f),
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
