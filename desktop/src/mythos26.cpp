// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/mythos26.h"

#include <algorithm>
#include <cmath>

#include "edmx/beat_clock.h"

using namespace edmx;

// ============================================================================
// beat_pulse
// ============================================================================

Pattern_Mythos_BeatPulse::Pattern_Mythos_BeatPulse()
    : clock(&sharedBeatClock())
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

void Pattern_Mythos_BeatPulse::reflect(ecore::PropertyBag& bag)
{
    bag.add("attack", attackSeconds, 0.0f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("decay", decaySeconds, 0.01f, 3.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("hold", bHoldOnRetrigger, [this] { setHoldOnRetrigger(bHoldOnRetrigger); });
}

void Pattern_Mythos_BeatPulse::init()
{
    started = false;
    level = 0.0f;
    envelope.reset();
}

void Pattern_Mythos_BeatPulse::setBeatsPerPulse(int beats)
{
    beatsPerPulse = std::max(beats, 1);
}

void Pattern_Mythos_BeatPulse::tick(float deltaTime)
{
    const double now = nowSeconds();

    // Divide the beat count, not the tempo. Dividing the tempo would stretch
    // the envelope with it and the hit would go soft at slower divisions; the
    // point of "on twos" is the same crack, half as often.
    const double position = clock->beatPosition(now) / static_cast<double>(beatsPerPulse);
    const long long beat = static_cast<long long>(std::floor(position));

    if (!started || beat != lastBeat)
    {
        started = true;
        lastBeat = beat;

        // Fire the impulse at where the beat actually landed, not at this
        // frame's boundary. At 40fps a frame is 25ms and a beat lands anywhere
        // inside one, so triggering at the boundary would quantise every pulse
        // to the frame grid and put a visible swing on the rig.
        envelope.triggerAt(static_cast<float>(clock->timeSinceBeat(now)));
    }

    // The trigger reads the curve's peak across the frame rather than its value
    // at the end of one, which is what keeps the envelope safe to shorten — see
    // eanim::AutomationCurve::peak.
    envelope.tick(deltaTime);

    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    level = floorValue + ((1.0f - floorValue) * std::clamp(envelope.getValue(), 0.0f, 1.0f));
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
    // On twos by default. The table can override it and so can the desk, but
    // this is the look's own idea of itself: with a lit wash underneath, a hit
    // on every beat is too much light and the hits stop reading as hits.
    pulse.setBeatsPerPulse(2);
}

void Pattern_Mythos_VuPulse::reflect(ecore::PropertyBag& bag)
{
    // The flash's own knobs first, under the names beat_pulse gives them, so
    // the same look tunes the same way whichever state you are in.
    pulse.reflect(bag);

    bag.add("base_gain", baseGain, 0.0f, 2.0f);
    bag.add("base_floor", baseFloor, 0.0f, 1.0f);
    bag.add("base_smoothing", baseSmoothing, 0.0f, 2.0f);
}

void Pattern_Mythos_VuPulse::init()
{
    pulse.init();
    baseLevel = 0.0f;
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
        return;
    }

    // Symmetric one-pole, framed in seconds rather than as a per-frame
    // coefficient so the look does not change with the frame rate. Equally slow
    // in both directions on purpose: an asymmetric filter that snaps upward
    // keeps every transient it is supposed to be removing.
    const float rate = 1.0f - std::exp(-deltaTime / baseSmoothing);
    baseLevel += (target - baseLevel) * rate;
}

void Pattern_Mythos_VuPulse::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    (void)node;

    const float flash = pulse.getLevel();

    // Desaturate toward white rather than blending to a white colour. Blending
    // would lerp the hue as well, and a hue on its way to an achromatic colour
    // passes through hues that are not in this look at all — a red base would
    // go orange mid-flash. Pulling saturation out leaves the hue alone and
    // arrives at exactly white.
    inOutColor = baseColor;
    inOutColor.setSaturationAlpha(baseColor.getSatFloat() * (1.0f - std::clamp(flash, 0.0f, 1.0f)));
    inOutColor.setBrightnessAlpha(std::max(baseLevel, flash));
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

    /// A look that fires on the beat, and can be told how often.
    ///
    /// `beats` is what it opens on; `beat div` at the desk overrides every
    /// look at once. The lambda is where the concrete type is known, which is
    /// what keeps the machine itself from needing to know about any of them.
    template <typename PatternT>
    StateDef beatLook(const char* name, int beats)
    {
        StateDef def = look<PatternT>(name);
        def.defaultBeatsPerPulse = beats;
        def.applyDivision = [](eanim::GeneratorHSV* generator, int beatsPerPulse) {
            static_cast<PatternT*>(generator)->setBeatsPerPulse(beatsPerPulse);
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
    // The number on a beatLook is what it opens on - 1 is every beat, 2 every
    // other, 4 once a bar. `beat div <n>` at the desk overrides all of them at
    // once, and `beat div 0` hands them back their own defaults.
    //
    // Renaming a state means renaming it in three places: here,
    // MYTHOS26_STATES in python/eclipse_dmx/config.py, and the button table in
    // python/eclipse_dmx/viewer.py. The executable is the authority; the other
    // two are so a config can be validated and a button can be labelled
    // without one running.
    // ------------------------------------------------------------------
    std::vector<StateDef> states = {
        beatLook<Pattern_Mythos_BeatPulse>("beat_pulse", 1),
        beatLook<Pattern_Mythos_VuPulse>("vu_pulse", 2),

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
