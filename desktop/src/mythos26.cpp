// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/mythos26.h"

#include <algorithm>
#include <cmath>

#include "lib/ecore/math.h"

#include "edmx/beat_clock.h"
#include "edmx/beat_trigger.h"

using namespace edmx;

namespace
{
    float frac(float v)
    {
        return v - std::floor(v);
    }

    /// The stage's height as 0..1 at this node - scanner::PatternScanner's
    /// stageAlpha, for the placeholder, which is not one.
    float stageAlphaOf(const eio::HSVStripNode* node)
    {
        if (node != nullptr && node->GetStripNodeType() == eio::StripNodeType::MAPPED2D)
        {
            const float y = static_cast<const eio::HSVStripNode_Mapped2D*>(node)->coord.y;
            return std::clamp((y - scanner::kStageBottom) / (scanner::kStageTop - scanner::kStageBottom),
                              0.0f, 1.0f);
        }
        return 0.0f;
    }
}

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
    // how far up the stage, 0..1 - the show's frame is the scanner's stage
    const float position = stageAlphaOf(node);

    const float breath = 0.5f + (0.5f * std::sin((phase + (position * 0.25f)) * 6.2831853f));

    inOutColor = ecore::HSV(hue, 0.7f, 1.0f);
    inOutColor.setBrightnessAlpha(0.10f + (0.15f * breath));
}

// ============================================================================
// noise wash
// ============================================================================

void Pattern_Mythos_NoiseWash::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(node);
    const float t = timeActive * 0.10f * speed;
    const float s = scale;

    // two octaves drifting against each other, so patches form and dissolve
    // rather than one sheet sliding past - the same field the colour clouds
    // ride, at a scale that gives the obelisk's face two or three patches
    const float n =
        0.65f * scanner::valueNoise(at.x * 0.30f * s + t * 1.9f, at.y * 0.16f * s - t * 1.2f)
      + 0.35f * scanner::valueNoise(at.x * 0.75f * s - t * 1.5f + 37.0f, at.y * 0.38f * s + t * 0.8f);

    // intensity is the field's depth: at 0 the wash sits flat at the middle
    // of its range, at 1 it runs the whole way from one colour to the other
    const float depth = std::clamp(intensity, 0.0f, 1.0f);
    const float mix = 0.5f + (n - 0.5f) * depth;

    // The brightness is the colours' own - a pair of a bright cyan and a
    // deep blue is bright where it is cyan and deep where it is blue, which
    // is what the pair says - lifted by the floor so the deep end of a pair
    // is never a fixture that looks unplugged.
    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    inOutColor = ecore::HSV::blend(colorA, colorB, mix);
    inOutColor.setBrightnessAlpha(floorValue + (1.0f - floorValue) * inOutColor.getValFloat());
}

void Pattern_Mythos_NoiseWash::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("speed", speed, 0.1f, 4.0f);
    bag.add("scale", scale, 0.3f, 3.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("color_a", colorA);
    bag.add("color_b", colorB);
}

// ============================================================================
// bus wash
// ============================================================================

void Pattern_Mythos_BusWash::init()
{
    bus = &sharedAudioLevel();
    level = 0.0f;
    hitLevel = 0.0f;
}

void Pattern_Mythos_BusWash::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }

    const double now = nowSeconds();

    // the wash: the channel, slewed. A channel nobody is filling reads zero,
    // which leaves the wash at its floor - lit, which is the point of one.
    const float target = std::clamp(bus->get(channel, now) * gain, 0.0f, 1.0f);
    if (slew <= 0.0f || deltaTime <= 0.0f)
    {
        level = target;
    }
    else
    {
        level += (target - level) * (1.0f - std::exp(-deltaTime / slew));
    }

    // the hit: a peak follower over a channel that is already a transient.
    // Up instantly - a hit two frames wide has to be caught - and down over
    // hitDecay, to the live value rather than to zero.
    const float hitNow = std::clamp(bus->get(hitChannel, now), 0.0f, 1.0f);
    if (hitNow >= hitLevel || hitDecay <= 0.0f || deltaTime <= 0.0f)
    {
        hitLevel = hitNow;
    }
    else
    {
        hitLevel += (hitNow - hitLevel) * (1.0f - std::exp(-deltaTime / hitDecay));
    }
}

void Pattern_Mythos_BusWash::render(eio::HSVStripNode* /*node*/, ecore::HSV& inOutColor) const
{
    // Intensity scales what rides above the floor - the wash's swing and the
    // hit both - so a low cue is the colour holding still at its floor, not
    // the colour gone.
    const float amount = std::clamp(intensity, 0.0f, 1.0f);
    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    const float wash = floorValue + (1.0f - floorValue) * level * amount;
    const float kick = std::clamp(hitLevel * hit * amount, 0.0f, 1.0f);

    inOutColor = ecore::HSV::blend(color, hitColor, kick);
    // the hit lifts the level as well as recolouring it: green landing on a
    // purple wash sitting at its floor should be a flash, not a tint
    inOutColor.setBrightnessAlpha(inOutColor.getValFloat() * std::max(wash, kick));
}

void Pattern_Mythos_BusWash::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 4.0f);
    bag.add("slew", slew, 0.0f, 1.0f);
    bag.add("color", color);
    bag.add("hit", hit, 0.0f, 1.0f);
    bag.add("hit_decay", hitDecay, 0.02f, 1.0f);
    bag.add("hit_color", hitColor);
}

// ============================================================================
// kick colour
// ============================================================================

void Pattern_Mythos_KickColor::init()
{
    bus = &sharedAudioLevel();
    wasAbove = false;
    kicks = 0;
    hue = 0.0f;
    flash = 0.0f;
}

void Pattern_Mythos_KickColor::onKick()
{
    ++kicks;
    // the golden angle: every step lands far from the last few, and the
    // sequence never closes into a cycle short enough to notice
    hue = std::fmod(hue + 137.508f, 360.0f);
    flash = 1.0f;
}

void Pattern_Mythos_KickColor::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }

    // one rising edge, one colour
    const bool above = bus->get(channel, nowSeconds()) >= threshold;
    if (above && !wasAbove)
    {
        onKick();
    }
    wasAbove = above;

    if (decay > 0.0f && deltaTime > 0.0f)
    {
        flash *= std::exp(-deltaTime / decay);
    }
    else
    {
        flash = 0.0f;
    }
}

void Pattern_Mythos_KickColor::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const unsigned int index = (node != nullptr)
        ? static_cast<unsigned int>(node->getStripIdx())
        : 0u;

    // each fixture's own nudge off the hue, re-dealt on every kick: hashed on
    // (fixture, kick) so it holds still between kicks and changes on one
    const float nudge = (hash01(index * 2654435761u + kicks * 40503u) - 0.5f) * scatter * 360.0f;

    const float amount = std::clamp(intensity, 0.0f, 1.0f);
    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    const float value = floorValue + (1.0f - floorValue) * flash * amount;

    inOutColor = ecore::HSV(std::fmod(hue + nudge + 360.0f, 360.0f), saturation, 1.0f);
    inOutColor.setBrightnessAlpha(value);
}

void Pattern_Mythos_KickColor::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("threshold", threshold, 0.05f, 1.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("decay", decay, 0.02f, 2.0f);
    bag.add("saturation", saturation, 0.0f, 1.0f);
    bag.add("scatter", scatter, 0.0f, 1.0f);
}

// ============================================================================
// canyon wave
// ============================================================================

Pattern_Mythos_CanyonWave::Pattern_Mythos_CanyonWave()
    : canyon({
        ecore::HSV(22.0f, 0.95f, 1.0f),    // canyon orange
        ecore::HSV(18.0f, 0.85f, 0.45f),   // shadowed rock
        ecore::HSV(35.0f, 0.75f, 0.30f),   // brown
        ecore::HSV(95.0f, 0.80f, 0.75f),   // the green of the floor
        ecore::HSV(205.0f, 0.85f, 0.95f),  // sky
        ecore::HSV(225.0f, 0.90f, 0.55f),  // deep blue
      })
    , triggers(&sharedTriggerRack())
{
    // Restart, not RestartHold: the envelope fits inside a beat at any
    // tempo this runs at, and a rainbow that held over would stop being a
    // hit and become a tint.
    envelope.retriggerMode = eanim::RetriggerMode::Restart;
    setEnvelope(attackSeconds, decaySeconds);
}

void Pattern_Mythos_CanyonWave::setEnvelope(float inAttackSeconds, float inDecaySeconds)
{
    attackSeconds = std::max(inAttackSeconds, 0.0f);
    decaySeconds  = std::max(inDecaySeconds, 0.001f);

    envelope.curve.clear();
    envelope.curve.addKey(0.0f, 0.0f);
    envelope.curve.addKey(attackSeconds, 1.0f, easing_functions::EaseOutCubic);
    envelope.curve.addKey(attackSeconds + decaySeconds, 0.0f);
}

void Pattern_Mythos_CanyonWave::setPulseRate(float pulsesPerBeat)
{
    pulseRate = snapPulseRate(pulsesPerBeat);
}

void Pattern_Mythos_CanyonWave::reset()
{
    PatternScanner::reset();
    envelope.reset();
    rainbow = 0.0f;
}

void Pattern_Mythos_CanyonWave::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);

    // the same shared trigger beat_pulse fires off, so this rainbow and a
    // flash layer on the same rate land on the same frame
    const BeatTrigger& trigger = triggers->forRate(pulseRate);
    if (trigger.fired)
    {
        envelope.triggerAt(trigger.sinceHit);
    }
    envelope.tick(deltaTime);

    rainbow = std::clamp(envelope.getValue(), 0.0f, 1.0f) * std::clamp(intensity, 0.0f, 1.0f);
}

void Pattern_Mythos_CanyonWave::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const float up = stageAlpha(node);

    // the canyon: the palette over the stage's height, moving down it -
    // subtracting time takes each band toward the floor
    const ecore::HSV rock = canyon.getColor(frac(up * waves - timeActive * speed));

    // the rainbow: the wheel over the same height, drifting slowly the other
    // way so two hits in a row are not the same picture
    const ecore::HSV wheel(frac(up + timeActive * 0.05f) * 360.0f, 1.0f, 1.0f);

    inOutColor = ecore::HSV::blend(rock, wheel, rainbow);
}

void Pattern_Mythos_CanyonWave::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("speed", speed, 0.02f, 1.0f);
    bag.add("waves", waves, 0.25f, 4.0f);
    bag.add("attack", attackSeconds, 0.0f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("decay", decaySeconds, 0.01f, 3.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("rate", pulseRate, kQuarterTime, kDoubleTime, [this] { setPulseRate(pulseRate); });
}

void Pattern_Mythos_CanyonWave::reflectCurves(eanim::CurveBag& bag)
{
    bag.add("envelope", envelope.curve);
}

// ============================================================================
// gradient strobe
// ============================================================================

Pattern_Mythos_GradientStrobe::Pattern_Mythos_GradientStrobe()
    : triggers(&sharedTriggerRack())
{
    // Restart: a strobe that held over would smear into a wash. The envelope
    // is far shorter than a beat at any tempo, so a retrigger never lands
    // mid-fall anyway.
    envelope.retriggerMode = eanim::RetriggerMode::Restart;
    setEnvelope(attackSeconds, decaySeconds);
}

void Pattern_Mythos_GradientStrobe::setEnvelope(float inAttackSeconds, float inDecaySeconds)
{
    attackSeconds = std::max(inAttackSeconds, 0.0f);
    decaySeconds  = std::max(inDecaySeconds, 0.001f);

    envelope.curve.clear();
    envelope.curve.addKey(0.0f, 0.0f);
    envelope.curve.addKey(attackSeconds, 1.0f, easing_functions::EaseOutCubic);
    envelope.curve.addKey(attackSeconds + decaySeconds, 0.0f);
}

void Pattern_Mythos_GradientStrobe::setPulseRate(float pulsesPerBeat)
{
    pulseRate = snapPulseRate(pulsesPerBeat);
}

void Pattern_Mythos_GradientStrobe::reset()
{
    PatternScanner::reset();
    envelope.reset();
    strobe = 0.0f;
}

void Pattern_Mythos_GradientStrobe::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);

    const BeatTrigger& trigger = triggers->forRate(pulseRate);
    if (trigger.fired)
    {
        envelope.triggerAt(trigger.sinceHit);
    }
    envelope.tick(deltaTime);

    strobe = std::clamp(envelope.getValue(), 0.0f, 1.0f) * std::clamp(intensity, 0.0f, 1.0f);
}

void Pattern_Mythos_GradientStrobe::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const float up = stageAlpha(node);

    // a triangle rather than a sawtooth, so the gradient runs purple to
    // white and back with no seam marching up the stage
    const float phase = frac(up * waves + timeActive * speed);
    const float mix = 1.0f - std::fabs(phase * 2.0f - 1.0f);

    const ecore::HSV gradient = ecore::HSV::blend(colorA, colorB, mix);
    inOutColor = ecore::HSV::blend(gradient, strobeColor, strobe);
}

void Pattern_Mythos_GradientStrobe::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("speed", speed, 0.05f, 3.0f);
    bag.add("waves", waves, 0.25f, 4.0f);
    bag.add("attack", attackSeconds, 0.0f, 0.5f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("decay", decaySeconds, 0.01f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("rate", pulseRate, kQuarterTime, kDoubleTime, [this] { setPulseRate(pulseRate); });
    bag.add("color_a", colorA);
    bag.add("color_b", colorB);
    bag.add("strobe_color", strobeColor);
}

void Pattern_Mythos_GradientStrobe::reflectCurves(eanim::CurveBag& bag)
{
    bag.add("envelope", envelope.curve);
}

// ============================================================================
// fire and rain, dressed for the show
// ============================================================================

void Pattern_Mythos_Fire::applyIntensity()
{
    // how eagerly the floor ignites: a few embers at the bottom of the knob,
    // the generic look's own default at the top
    sparking = lerp(0.12f, 0.47f, std::clamp(intensity, 0.0f, 1.0f));
}

void Pattern_Mythos_Fire::reflect(ecore::PropertyBag& bag)
{
    // first, so it sits where it does on every other cue; `sparking` below it
    // is the same number under its own name, and the last one turned wins
    bag.add("intensity", intensity, 0.0f, 1.0f, [this] { applyIntensity(); });
    Pattern_Generic_Fire2012::reflect(bag);
}

Pattern_Mythos_Rain::Pattern_Mythos_Rain()
{
    hueSpread = 1.0f;   // every drop its own colour
    applyIntensity();
}

void Pattern_Mythos_Rain::applyIntensity()
{
    // a few drops at the bottom of the knob, a downpour at the top
    dropCount = lerp(4.0f, 22.0f, std::clamp(intensity, 0.0f, 1.0f));
}

void Pattern_Mythos_Rain::reset()
{
    Pattern_Generic_MatrixRain::reset();

    // Roll the storm forward until the first drops have crossed the stage:
    // spawned up to tail + 20 above the top and falling at 0.6..1.4 of
    // fall_speed, the slowest needs (tail + 20 + stage) / (0.6 * speed)
    // seconds to clear the floor. Small steps, so the particles integrate
    // the same path they would have under real frames.
    const float span = tailLength + 20.0f + (scanner::kStageTop - scanner::kStageBottom);
    const float seconds = span / std::max(0.6f * fallSpeed, 0.1f);
    const float step = 1.0f / 30.0f;
    for (float rolled = 0.0f; rolled < seconds; rolled += step)
    {
        Pattern_Generic_MatrixRain::tick(step);
    }
    // the clock is the cue's, not the pre-roll's: the churn hashes off it,
    // and nothing else here reads it
    timeActive = 0.0f;
}

void Pattern_Mythos_Rain::reflect(ecore::PropertyBag& bag)
{
    bag.add("intensity", intensity, 0.0f, 1.0f, [this] { applyIntensity(); });
    Pattern_Generic_MatrixRain::reflect(bag);
}

// ============================================================================
// hue cycle
// ============================================================================

void Pattern_Mythos_HueCycle::tick(float deltaTime)
{
    phase = frac(phase + deltaTime * speed);
}

void Pattern_Mythos_HueCycle::render(eio::HSVStripNode* /*node*/, ecore::HSV& inOutColor) const
{
    inOutColor = ecore::HSV(phase * 360.0f, saturation, 1.0f);
    inOutColor.setBrightnessAlpha(std::clamp(level, 0.0f, 1.0f));
}

void Pattern_Mythos_HueCycle::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", speed, 0.02f, 2.0f);
    bag.add("saturation", saturation, 0.0f, 1.0f);
    bag.add("level", level, 0.0f, 1.0f);
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
    /// The UV par's `off` and `on` are this at 0 and 1. `level` is a knob so
    /// `on` can be trimmed at the desk without becoming a different state -
    /// and so a mod can turn it, which is what makes the same class the
    /// additive hit layers of config/audio_layers_test.json.
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

    /// A show look on the stage: a scanner::PatternScanner, wrapped so its
    /// clock is wound back on entry - the same State_ScannerHSV the generic
    /// machine uses, and for the same reason: the rain and the fire start
    /// empty on every visit rather than mid-storm.
    ///
    /// `setup` is run on the fresh look before init, and is where a cue
    /// states its colours or its channel: two cues on one class differ only
    /// in what it is handed, and the difference reads as a line each.
    template <typename PatternT>
    StateDef showLook(const char* name, std::function<void(PatternT&)> setup = {})
    {
        StateDef def;
        def.name = name;
        def.make = [setup]() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<PatternT>();
            if (setup)
            {
                setup(*pattern);
            }
            return pattern;
        };
        def.makeState = [](const char* stateName, eio::RelicIO* io,
                           std::shared_ptr<eanim::GeneratorHSV> generator) {
            return std::static_pointer_cast<State_GenericHSV>(
                std::make_shared<scanner::State_ScannerHSV>(stateName, io,
                    std::static_pointer_cast<scanner::PatternScanner>(generator)));
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
    //
    // `kick` and `rainbow` are the show's asks: the blacklight on every kick
    // for filter blown, and breathing through a hue wheel - which on three
    // banks of one colour is a third to two thirds and never off - under the
    // rain. Both are looks the show has anyway, pointed at one light.
    std::vector<StateDef> states = {
        levelLook("off", 0.0f),
        //                                     attack decay  rate  entry hit
        beatLook<Pattern_Mythos_BeatPulse>("flash", 0.02f, 0.30f, 1.0f, false),
        levelLook("on", 1.0f),
        showLook<Pattern_Mythos_BusWash>("kick", [](Pattern_Mythos_BusWash& look) {
            look.color = ecore::HSV(0.0f, 0.0f, 1.0f);
            look.channel = AudioChannel::BassHits;
            look.floorLevel = 0.0f;
            look.slew = 0.0f;
            look.init();
        }),
        look<Pattern_Mythos_HueCycle>("rainbow"),
    };

    CoordFrame frame;
    // a short cross-fade: a light changing mode should snap, not swim
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "uv", std::move(states), 0, frame, 0.15f));
}

std::unique_ptr<StateMachinePattern> edmx::makeFlashStateMachine()
{
    // The bpm flash, as a layer over the whole rig. The same beat pulse the
    // UV runs, white unless its `color` knob says otherwise, and off until a
    // cue asks - at `blend: add` a layer at black is not there, so the cues
    // that do not want it never know it is patched.
    //
    // No `on`: a solid white summed over every fixture is a whiteout, not a
    // mode anyone would pick from a pad.
    std::vector<StateDef> states = {
        levelLook("off", 0.0f),
        //                                     attack decay  rate  entry hit
        beatLook<Pattern_Mythos_BeatPulse>("flash", 0.02f, 0.30f, 1.0f, false),
    };

    CoordFrame frame;
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "flash", std::move(states), 0, frame, 0.15f));
}

std::unique_ptr<StateMachinePattern> edmx::makeMythos26StateMachine()
{
    // ------------------------------------------------------------------
    // The show, one line per cue, in the order a UI shows them - and the
    // order of "pattern spec.txt" at the top of the checkout, which is where
    // the numbers on the surface come from. Sixteen cues, of which the spec
    // has so far named eight; a slot that exists is a cue that can be
    // switched to, mapped to a button and seen on the rig before there is a
    // look in it, so the rest are placeholders, tinted so they are told
    // apart.
    //
    // What a cue asks of the *rest* of the room - its Synesthesia scene and
    // media, the flash layer, the UV - is not here. A look is a look; the
    // cue table in config/mythos-show.json says what goes with it, and the
    // surface fires the lot from one pad.
    //
    // Writing one means adding a GeneratorHSV to mythos26.h/.cpp and changing
    // its line here; nothing else in the runner, the protocol or the UI needs
    // to know.
    //
    // Renaming a state means renaming it in three places: here,
    // MYTHOS26_STATES in python/eclipse_dmx/config.py, and the button table in
    // python/eclipse_dmx/viewer.py. The executable is the authority; the other
    // two are so a config can be validated and a button can be labelled
    // without one running.
    // ------------------------------------------------------------------
    using ecore::HSV;

    std::vector<StateDef> states = {
        // 1. cyan into deep blue, drifting - the neuron scene's own colours
        showLook<Pattern_Mythos_NoiseWash>("neuron", [](Pattern_Mythos_NoiseWash& look) {
            look.colorA = HSV(185.0f, 0.90f, 1.00f);
            look.colorB = HSV(232.0f, 1.00f, 0.40f);
        }),
        // 2. red, riding the mids, never below a fifth; the flash layer is
        //    the cue's other half
        showLook<Pattern_Mythos_BusWash>("geode", [](Pattern_Mythos_BusWash& look) {
            look.color = HSV(0.0f, 1.0f, 1.0f);
            look.channel = AudioChannel::MidPresence;
            look.floorLevel = 0.2f;
            look.init();
        }),
        // 3. the rain, every drop its own colour
        showLook<Pattern_Mythos_Rain>("rain"),
        // 4. fire
        showLook<Pattern_Mythos_Fire>("fire"),
        // 5. a new colour on every kick
        showLook<Pattern_Mythos_KickColor>("glitch", [](Pattern_Mythos_KickColor& look) {
            look.init();
        }),
        // 6. pink into purple, drifting - the fire tunnel behind it
        showLook<Pattern_Mythos_NoiseWash>("tunnel", [](Pattern_Mythos_NoiseWash& look) {
            look.colorA = HSV(325.0f, 0.85f, 1.00f);
            look.colorB = HSV(275.0f, 1.00f, 0.55f);
        }),
        // 7. purple riding the mids, never below 0.3, green on the kick
        showLook<Pattern_Mythos_BusWash>("blown", [](Pattern_Mythos_BusWash& look) {
            look.color = HSV(285.0f, 0.95f, 1.0f);
            look.channel = AudioChannel::MidPresence;
            look.floorLevel = 0.3f;
            look.hitColor = HSV(120.0f, 1.0f, 1.0f);
            look.hit = 1.0f;
            look.init();
        }),
        // 8. punk purple into white, strobing on the beat; the flash layer
        //    and the UV on the kick are the cue's other half
        showLook<Pattern_Mythos_GradientStrobe>("punk"),
        placeholder("slot_9", 240.0f),
        // 10. the canyon fly-through, and a rainbow on the beat
        showLook<Pattern_Mythos_CanyonWave>("canyon"),
        placeholder("slot_11", 300.0f),
        placeholder("slot_12", 330.0f),
        placeholder("slot_13", 0.0f),
        placeholder("slot_14", 30.0f),
        placeholder("slot_15", 60.0f),
        placeholder("slot_16", 90.0f),
    };

    // ------------------------------------------------------------------
    // The stage's space - the same frame the generic machine runs in.
    //
    // The show borrows the fire and the rain from that list, and every look
    // written here stands on the same stage so a wave that pours down the
    // obelisk pours through the ring and past the truss in one motion. The
    // environment places the obelisk and the ring in literal stage
    // coordinates, so this frame only ever decides where a *normalized*
    // device - the truss - is stretched to: the default puts it up the
    // stage's height beside the pillar, which is what a top-down cue wants.
    // ------------------------------------------------------------------
    CoordFrame stage;

    // Shorter than the jacket's 0.4s. These are cues in a show rather than
    // moods on a garment, and a beat-locked look wants to arrive promptly.
    const float transitionTime = 0.25f;

    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "mythos26", std::move(states),
        /*segmentId*/ 0, // nothing here branches on segment; only relic looks do
        stage,
        transitionTime));
}
