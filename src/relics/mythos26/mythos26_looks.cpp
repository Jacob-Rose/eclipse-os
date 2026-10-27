// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "mythos26_looks.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

#include <ecore/math.h>

using namespace mythos26;

namespace
{
    float frac(float v)
    {
        return v - std::floor(v);
    }

    /// Whether this node is the visualiser's probe - see NodeSpace::Probe.
    bool isProbe(const eio::HSVStripNode* node)
    {
        const eio::HSVStripNode_Space* spaced = eio::spaceOf(node);
        return spaced != nullptr && spaced->space == eio::NodeSpace::Probe;
    }

    /// A colour with its hue turned by `turns` of the wheel.
    ecore::HSV turnHue(const ecore::HSV& color, float turns)
    {
        if (turns == 0.0f)
        {
            return color;
        }
        ecore::HSV out(std::fmod(color.getHueFloat() + turns * 360.0f + 360.0f, 360.0f),
                       color.getSatFloat(), color.getValFloat());
        out.setBrightnessAlpha(color.getValFloat());
        return out;
    }

    /// A palette that loops: `t` 0..1 runs through every colour and back to
    /// the first, so a scrolling field has no seam. The HSVPalette the
    /// generic looks use clamps at its ends, which on the canyon was a hard
    /// edge marching down the stage once a cycle.
    ///
    /// Blended round the short side of the wheel - blendHsv - so a band
    /// stays a colour on its way to the next: this was an RGB mix, and
    /// between two saturated bands an RGB mix is a grey, which with the
    /// level put back over it is a light grey. Six bands of canyon with
    /// five smears of grey between them, on ten pars, was most of the
    /// truss.
    ///
    /// And eased between the knots, not straight. A straight blend has a
    /// corner at every colour - the value falling from the orange at one
    /// rate and rising toward the green at another - and on the obelisk's
    /// forty rows each corner was a line across the pillar, six of them
    /// marching down it once a cycle. Eased, each band is a plateau of its
    /// own colour and the blend into the next has no edge at either end.
    class LoopPalette
    {
    public:
        LoopPalette(std::initializer_list<ecore::HSV> inColors) : colors(inColors) {}

        ecore::HSV at(float t) const
        {
            if (colors.empty())
            {
                return ecore::HSV();
            }
            const float scaled = frac(t) * static_cast<float>(colors.size());
            const size_t index = static_cast<size_t>(scaled) % colors.size();
            const size_t next = (index + 1) % colors.size();
            const float f = scaled - static_cast<float>(index);
            return blendHsv(colors[index], colors[next], f * f * (3.0f - 2.0f * f));
        }

    private:
        std::vector<ecore::HSV> colors;
    };
}

// ============================================================================
// modes
// ============================================================================

void ShowModes::reflectMode(ecore::PropertyBag& bag)
{
    // the maximum is the count: it is how the desk learns how many modes
    // this cue has, and so how far its pad steps before coming round
    bag.add("mode", mode, 1.0f, static_cast<float>(modeCount()), [this] { applyMode(); });
}

void ShowModes::keepAcrossModes(std::initializer_list<const char*> names)
{
    kept.assign(names.begin(), names.end());
}

void ShowModes::initModes(eanim::GeneratorHSV& inLook, std::vector<std::function<void()>> inVariants)
{
    look = &inLook;
    variants = std::move(inVariants);
    mode = 1.0f;

    // the snapshot: every knob the cue opened with, by name, so a mode can be
    // undone by writing them back through the same bag the desk writes -
    // less the ones flown by hand, which no mode owns
    baseValues.clear();
    baseColors.clear();
    ecore::PropertyBag bag;
    look->reflect(bag);
    for (const ecore::Property& property : bag.all())
    {
        if (property.name == "mode"
            || std::find(kept.begin(), kept.end(), property.name) != kept.end())
        {
            continue;
        }
        if (property.type == ecore::Property::Type::Color)
        {
            baseColors.emplace_back(property.name, property.getColor());
        }
        else
        {
            baseValues.emplace_back(property.name, property.get());
        }
    }
}

void ShowModes::applyMode()
{
    mode = std::clamp(std::round(mode), 1.0f, static_cast<float>(modeCount()));
    if (look == nullptr)
    {
        return;
    }

    // Back to the cue first, whatever mode this is, so a variant is a diff
    // on the cue and not on the last variant. Through the bag rather than
    // the fields, so a knob with a derived value - an envelope - rebuilds.
    ecore::PropertyBag bag;
    look->reflect(bag);
    for (const auto& [name, value] : baseValues)
    {
        bag.set(name, value);
    }
    for (const auto& [name, color] : baseColors)
    {
        bag.setColor(name, color);
    }

    const int index = getMode() - 2;
    if (index >= 0 && index < static_cast<int>(variants.size()) && variants[index])
    {
        variants[index]();
    }
}

void ShowModes::resetMode()
{
    // Only a look left in another mode is touched: a knob tuned at the desk
    // in mode 1 survives a cue change the way it did before modes existed.
    if (getMode() != 1)
    {
        mode = 1.0f;
        applyMode();
    }
}

// ============================================================================
// the truss as a row
// ============================================================================

bool mythos26::trussRowCoord(const eio::HSVStripNode* node, float row, ecore::Coordinate& outAt)
{
    const eio::HSVStripNode_Space* spaced = eio::spaceOf(node);
    if (spaced == nullptr || spaced->space != eio::NodeSpace::Truss)
    {
        return false;
    }
    // `u` is 0..1 along the truss in wiring order; spread over the obelisk's
    // runs, so the pars read the same stretch of stage the runs do
    outAt = ecore::Coordinate(spaced->u * static_cast<float>(scanner::kStageColumns - 1), row);
    return true;
}

namespace
{
    bool isTruss(const eio::HSVStripNode* node)
    {
        const eio::HSVStripNode_Space* spaced = eio::spaceOf(node);
        return spaced != nullptr && spaced->space == eio::NodeSpace::Truss;
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
// house
// ============================================================================

void Pattern_Mythos_House::render(eio::HSVStripNode* /*node*/, ecore::HSV& inOutColor) const
{
    inOutColor = color;
    inOutColor.setBrightnessAlpha(color.getValFloat() * std::clamp(level, 0.0f, 1.0f)
                                  * std::clamp(intensity, 0.0f, 1.0f));
}

void Pattern_Mythos_House::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("level", level, 0.0f, 1.0f);
    bag.add("color", color);
}

// ============================================================================
// noise wash
// ============================================================================

void Pattern_Mythos_NoiseWash::init()
{
    bus = &sharedAudioLevel();
    level = 0.0f;
    hueOffset = 0.0f;
    fieldTime = 0.0f;
    kickLevel = 0.0f;
}

void Pattern_Mythos_NoiseWash::reset()
{
    Pattern_MythosLook::reset();
    fieldTime = 0.0f;
    kickLevel = 0.0f;
}

ecore::HSV Pattern_Mythos_NoiseWash::turned(const ecore::HSV& color) const
{
    return turnHue(color, hueOffset);
}

void Pattern_Mythos_NoiseWash::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);
    // the wheel turns whether or not anything follows the bus - it is the
    // palette, not the level
    hueOffset = hueCycle > 0.0f ? frac(hueOffset + deltaTime * hueCycle) : 0.0f;
    fieldTime += std::max(deltaTime, 0.0f) * (1.0f + std::max(push, 0.0f) * level);
    if (follow <= 0.0f && push <= 0.0f && kick <= 0.0f)
    {
        return;     // nothing reads the bus; leave the level where it was
    }
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }

    // the same slewed read the bus wash does, so the two breathe alike
    const double now = nowSeconds();
    const float target = std::clamp(bus->get(channel, now) * gain, 0.0f, 1.0f);
    if (slew <= 0.0f || deltaTime <= 0.0f)
    {
        level = target;
    }
    else
    {
        level += (target - level) * (1.0f - std::exp(-deltaTime / slew));
    }

    // the kick: up at once, down over kick_decay - the bus wash's follower
    if (kick > 0.0f)
    {
        const float hitNow = std::clamp(bus->get(AudioChannel::BassHits, now), 0.0f, 1.0f);
        const float seconds = (hitNow >= kickLevel) ? kickAttack : kickDecay;
        if (seconds <= 0.0f || deltaTime <= 0.0f)
        {
            kickLevel = hitNow;
        }
        else
        {
            kickLevel += (hitNow - kickLevel) * (1.0f - std::exp(-deltaTime / seconds));
        }
    }
}

void Pattern_Mythos_NoiseWash::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const ecore::HSV a = turned(colorA);
    const ecore::HSV b = turned(colorB);

    // the probe is the palette's first colour, so the scene behind the rig
    // is keyed to the cue and not to whichever patch is passing the truss
    if (isProbe(node))
    {
        inOutColor = a;
        inOutColor.setBrightnessAlpha(a.getValFloat());
        return;
    }

    const Coordinate at = nodeCoord(node);
    const float t = fieldTime * 0.10f * speed;
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
    float mix = 0.5f + (n - 0.5f) * depth;

    // contrast: the mix pushed toward whichever side it leans, by a power
    // under one on its distance from the middle - a curve, not a step, so
    // the two colours still meet, in a narrower band the harder it is set
    const bool truss = isTruss(node);
    const float pick = std::clamp((truss && trussContrast >= 0.0f) ? trussContrast : contrast, 0.0f, 1.0f);
    if (pick > 0.0f)
    {
        const float x = mix * 2.0f - 1.0f;
        mix = 0.5f + 0.5f * std::copysign(std::pow(std::fabs(x), 1.0f - 0.85f * pick), x);
    }

    // The brightness is the colours' own - a pair of a bright cyan and a
    // deep blue is bright where it is cyan and deep where it is blue, which
    // is what the pair says - lifted by the floor so the deep end of a pair
    // is never a fixture that looks unplugged.
    //
    // And the track's, when the wash follows a channel: what rides above the
    // floor is scaled by the channel's level, `follow` being how much of
    // that scaling applies.
    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    const float ride = 1.0f - std::clamp(follow, 0.0f, 1.0f) * (1.0f - level);
    inOutColor = blendRgb(a, b, mix);
    const float value = floorValue + (1.0f - floorValue) * inOutColor.getValFloat() * ride;
    const float reach = truss ? std::clamp(trussKick, 0.0f, 1.0f) : 1.0f;
    const float punch = std::clamp(kick * kickLevel * depth * reach, 0.0f, 1.0f);
    inOutColor.setBrightnessAlpha(value + (1.0f - value) * punch);
}

void Pattern_Mythos_NoiseWash::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("speed", speed, 0.1f, 4.0f);
    bag.add("scale", scale, 0.3f, 3.0f);
    bag.add("contrast", contrast, 0.0f, 1.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("follow", follow, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 4.0f);
    bag.add("slew", slew, 0.0f, 1.0f);
    bag.add("color_a", colorA);
    bag.add("color_b", colorB);
    bag.add("hue_cycle", hueCycle, 0.0f, 1.0f);
    bag.add("push", push, 0.0f, 3.0f);
    bag.add("kick", kick, 0.0f, 1.0f);
    bag.add("kick_decay", kickDecay, 0.05f, 1.0f);
    bag.add("kick_attack", kickAttack, 0.0f, 0.3f);
    bag.add("truss_kick", trussKick, 0.0f, 1.0f);
    bag.add("truss_contrast", trussContrast, -1.0f, 1.0f);
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
    float reading = bus->get(channel, now);
    for (AudioChannel also : alsoChannels)
    {
        reading = std::max(reading, bus->get(also, now));
    }
    const float target = std::clamp(reading * gain, 0.0f, 1.0f);
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

void Pattern_Mythos_BusWash::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    // Intensity scales what rides above the floor - the wash's swing and the
    // hit both - so a low cue is the colour holding still at its floor, not
    // the colour gone.
    const float amount = std::clamp(intensity, 0.0f, 1.0f);
    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    float wash = floorValue + (1.0f - floorValue) * level * amount;
    const float reach = isTruss(node) ? std::clamp(trussHit, 0.0f, 1.0f) : 1.0f;
    const float kick = std::clamp(hitLevel * hit * amount * reach, 0.0f, 1.0f);

    // the grain: a fine field drifting over the stage, cutting into the wash
    // where it is low - so the flat blue of the geode's second mode has
    // something moving in it. The floor stays the floor.
    if (texture > 0.0f)
    {
        const Coordinate at = nodeCoord(node);
        const float t = timeActive * 0.7f;
        const float grain = scanner::valueNoise(at.x * 1.1f + t * 0.9f, at.y * 0.9f - t * 1.3f);
        wash = floorValue + (wash - floorValue) * (1.0f - std::clamp(texture, 0.0f, 1.0f) * (1.0f - grain));
    }

    // The hit's shape. Plain: the hit colour lands on the kick and fades with
    // it. Afterglow: the kick is a pop of the wash's own colour to full for
    // the first third of its fall, and the hit colour is what it leaves
    // behind - swung to as the pop falls, held bright, then let go as the
    // wash comes back through. Blown pops pink and glows green after.
    const float glow = std::clamp((0.75f - kick) / 0.35f, 0.0f, 1.0f)
                     * std::clamp(kick / 0.25f, 0.0f, 1.0f);
    const float pop = std::clamp(kick * 1.5f, 0.0f, 1.0f);
    const float shape = std::clamp(afterglow, 0.0f, 1.0f);
    float share = std::lerp(kick, glow, shape);
    const float lift = std::lerp(kick, std::max(pop, glow * 0.85f), shape);

    ecore::HSV base = color;
    if (blend > 0.0f || accent < 1.0f)
    {
        const Coordinate at = nodeCoord(node);
        const float t = timeActive * 0.15f;
        if (blend > 0.0f)
        {
            const float drift = scanner::valueNoise(at.x * 0.25f + t, at.y * 0.12f - t * 0.6f);
            base = blendHsv(color, color2, std::clamp(blend, 0.0f, 1.0f) * drift);
        }
        if (accent < 1.0f)
        {
            // a different field from the pinks', so the green does not
            // always land on the rose
            const float patch = scanner::valueNoise(at.x * 0.3f - t * 0.8f + 17.0f, at.y * 0.2f + t);
            const float line = 1.0f - std::clamp(accent, 0.0f, 1.0f);
            share *= std::clamp((patch - line) / 0.12f + 0.5f, 0.0f, 1.0f);
        }
    }

    inOutColor = blendRgb(base, hitColor, share);
    // the hit lifts the level as well as recolouring it: green landing on a
    // purple wash sitting at its floor should be a flash, not a tint
    inOutColor.setBrightnessAlpha(inOutColor.getValFloat() * std::max(wash, lift));
}

void Pattern_Mythos_BusWash::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 4.0f);
    bag.add("slew", slew, 0.0f, 1.0f);
    bag.add("texture", texture, 0.0f, 1.0f);
    bag.add("color", color);
    bag.add("hit", hit, 0.0f, 1.0f);
    bag.add("hit_decay", hitDecay, 0.02f, 1.0f);
    bag.add("hit_color", hitColor);
    bag.add("afterglow", afterglow, 0.0f, 1.0f);
    bag.add("truss_hit", trussHit, 0.0f, 1.0f);
    bag.add("color_2", color2);
    bag.add("blend", blend, 0.0f, 1.0f);
    bag.add("accent", accent, 0.0f, 1.0f);
}

// ============================================================================
// kick colour
// ============================================================================

void Pattern_Mythos_KickColor::init()
{
    bus = &sharedAudioLevel();
    wasAbove = false;
    sinceKick = 1000.0f;
    kicks = 0;
    hue = 0.0f;
    flash = 0.0f;
    lastHue = 0.0f;
    lastFlash = 0.0f;
}

void Pattern_Mythos_KickColor::onKick()
{
    ++kicks;
    sinceKick = 0.0f;
    lastHue = hue;
    lastFlash = flash;
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

    // one rising edge, one colour - none inside the holdoff, so a detector
    // firing twice on one busy beat re-deals the rig once, and none while
    // the gate says nothing is playing
    const double now = nowSeconds();
    sinceKick += std::max(deltaTime, 0.0f);

    float gap = holdoff;
    const float bpm = sharedBeatClock().getBpm();
    if (beatHoldoff > 0.0f && bpm > 0.0f)
    {
        gap = std::max(gap, beatHoldoff * 60.0f / bpm);
    }

    bool playing = gateChannels.empty();
    for (AudioChannel gated : gateChannels)
    {
        playing = playing || bus->get(gated, now) >= gate;
    }

    const bool above = bus->get(channel, now) >= threshold;
    if (above && !wasAbove && sinceKick >= gap && playing)
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

float Pattern_Mythos_KickColor::dealtHue(unsigned int deal, float base, float up,
                                         unsigned int index) const
{
    float shifted = base;
    if (split > 0.0f && slices >= 1.0f)
    {
        // the band lines move with every deal too, so a tear never lands
        // twice in the same place
        const float offset = hash01(deal * 2246822519u + 7u);
        const unsigned int band = static_cast<unsigned int>(std::floor(up * slices + offset));
        if (hash01(band * 3266489917u + deal * 668265263u) >= 0.5f)
        {
            shifted += split * 360.0f;
        }
    }

    // each fixture's own nudge off the hue, re-dealt on every kick: hashed on
    // (fixture, kick) so it holds still between kicks and changes on one
    const float nudge = (hash01(index * 2654435761u + deal * 40503u) - 0.5f) * scatter * 360.0f;
    return std::fmod(shifted + nudge + 720.0f, 360.0f);
}

void Pattern_Mythos_KickColor::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const unsigned int index = (node != nullptr)
        ? static_cast<unsigned int>(node->getStripIdx())
        : 0u;
    const float up = stageAlpha(node);

    unsigned int deal = kicks;
    float base = hue;
    float lit = flash;
    float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);

    if (trussFloor >= 0.0f && isTruss(node))
    {
        // the deal climbs the truss: below the ripple a par still shows the
        // last one, falling as it was
        floorValue = std::clamp(trussFloor, 0.0f, 1.0f);
        const float arrive = std::max(trussSweep, 0.0f) * up;
        const float falling = decay > 0.0f ? decay : 0.001f;
        if (sinceKick < arrive && kicks > 0)
        {
            deal = kicks - 1;
            base = lastHue;
            lit = lastFlash * std::exp(-sinceKick / falling);
        }
        else
        {
            lit = (kicks > 0) ? std::exp(-(sinceKick - arrive) / falling) : 0.0f;
        }
    }

    const float amount = std::clamp(intensity, 0.0f, 1.0f);
    const float value = floorValue + (1.0f - floorValue) * lit * amount;

    inOutColor = ecore::HSV(dealtHue(deal, base, up, index), saturation, 1.0f);
    inOutColor.setBrightnessAlpha(value);
}

void Pattern_Mythos_KickColor::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("threshold", threshold, 0.05f, 1.0f);
    bag.add("holdoff", holdoff, 0.0f, 2.0f);
    bag.add("beat_holdoff", beatHoldoff, 0.0f, 4.0f);
    bag.add("gate", gate, 0.0f, 1.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("decay", decay, 0.02f, 2.0f);
    bag.add("saturation", saturation, 0.0f, 1.0f);
    bag.add("scatter", scatter, 0.0f, 1.0f);
    bag.add("slices", slices, 1.0f, 16.0f);
    bag.add("split", split, 0.0f, 1.0f);
    bag.add("truss_floor", trussFloor, -1.0f, 1.0f);
    bag.add("truss_sweep", trussSweep, 0.0f, 0.5f);
}

// ============================================================================
// canyon wave
// ============================================================================

namespace
{
    /// The fly-through, as one loop: rock, shadow, the floor, the sky, and
    /// back into rock without a seam. Each band is a colour and the next
    /// is near it on the wheel, so the short way round between them is a
    /// canyon colour too: the orange into the rust and the brown, the
    /// brown up through olive to the floor's green, the green through
    /// teal to the sky, and the deep blue back to orange the long way -
    /// which is through violet and magenta, a dusk. See LoopPalette.
    const LoopPalette kCanyon{
        ecore::HSV(22.0f, 0.95f, 1.00f),   // canyon orange
        ecore::HSV(12.0f, 0.90f, 0.55f),   // shadowed rock, rust
        ecore::HSV(30.0f, 0.80f, 0.35f),   // brown
        ecore::HSV(100.0f, 0.75f, 0.65f),  // the green of the floor
        ecore::HSV(205.0f, 0.80f, 0.90f),  // sky
        ecore::HSV(228.0f, 0.90f, 0.50f),  // deep blue
    };
}

Pattern_Mythos_CanyonWave::Pattern_Mythos_CanyonWave()
    : triggers(&sharedTriggerRack())
{
    // RestartHold, like beat_pulse, and for the same reason: the rise and
    // the fall together are longer than a beat at a club tempo, and a beat
    // landing in the fall would otherwise snap the rainbow to nothing and
    // start it over - a flicker on every beat. Held, the rainbow swells
    // between wherever the fall got to and full.
    envelope.retriggerMode = eanim::RetriggerMode::RestartHold;
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
    Pattern_MythosLook::reset();
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
    const ecore::HSV rock = kCanyon.at(up * waves - timeActive * speed);

    // The hit turns the canyon's colours toward a rainbow; it does not
    // replace them. Each band's hue swings by up to `rainbow_turn` of the
    // wheel, one way or the other by height - a sine over the stage,
    // drifting slowly so two hits in a row are not the same picture - so
    // the bands fan out round the wheel. This was a blend toward a wheel
    // hue the short way round, and where that hue sat opposite a band the
    // short way flipped sides from one frame to the next: a half-wheel
    // jump, mid-pulse, on whatever par was there. The turn has no
    // opposite to flip at.
    //
    // The bands keep their own levels - the rust stays dim and the sky
    // bright - lifted by `rainbow_lift` so the dark ones are seen to turn.
    // The truss gets `truss_rainbow` of all of it: a par is one lamp
    // lighting the room, and a swing that reads as a colour moving on the
    // obelisk is a colour strobe on the pars.
    const float reach = isTruss(node) ? std::clamp(trussRainbow, 0.0f, 1.0f) : 1.0f;
    const float amount = rainbow * reach;
    const float swing = std::sin((up + timeActive * 0.05f) * 2.0f * 3.14159265f);
    const ecore::HSV turned = turnHue(rock, amount * rainbowTurn * swing);

    const float lift = std::clamp(rainbowLift, 0.0f, 1.0f);
    const float value = rock.getValFloat();
    const float level = std::lerp(value, value + (1.0f - value) * lift, amount);
    ecore::HSV out(turned.getHueFloat(), std::lerp(rock.getSatFloat(), 1.0f, amount), level);
    out.setBrightnessAlpha(level);
    inOutColor = out;
}

void Pattern_Mythos_CanyonWave::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("speed", speed, 0.02f, 1.0f);
    bag.add("waves", waves, 0.25f, 4.0f);
    bag.add("attack", attackSeconds, 0.0f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("decay", decaySeconds, 0.01f, 3.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("rate", pulseRate, kQuarterTime, kDoubleTime, [this] { setPulseRate(pulseRate); });
    bag.add("rainbow_lift", rainbowLift, 0.0f, 1.0f);
    bag.add("rainbow_turn", rainbowTurn, 0.0f, 0.5f);
    bag.add("truss_rainbow", trussRainbow, 0.0f, 1.0f);
    bag.add("envelope", envelope.curve);
}

// ============================================================================
// gradient strobe
// ============================================================================

Pattern_Mythos_GradientStrobe::Pattern_Mythos_GradientStrobe()
    : triggers(&sharedTriggerRack())
{
    // Restart: a strobe that held over would smear into a wash. The envelope
    // is shorter than a beat at any tempo this runs at, so a retrigger never
    // lands mid-fall anyway - except in double time, where the dip is nearly
    // out by the next hit and the snap is nothing.
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
    Pattern_MythosLook::reset();
    envelope.reset();
    strobe = 0.0f;
    level = 0.0f;
    trussLevel = 0.0f;
    invert = 0.0f;
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

    // the channel, slewed - the same read every cue on the bus does, only
    // quicker, so the drop tracks the meter rather than trailing it
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }
    const float target = std::clamp(bus->get(channel, nowSeconds()) * gain, 0.0f, 1.0f);
    if (slew <= 0.0f || deltaTime <= 0.0f)
    {
        level = target;
    }
    else
    {
        level += (target - level) * (1.0f - std::exp(-deltaTime / slew));
    }

    // the drop is the channel's or the beat's by `follow`: the cue rides
    // the meter, the half- and double-time modes strobe on the grid
    const float onBeat = std::clamp(envelope.getValue(), 0.0f, 1.0f);
    strobe = std::lerp(onBeat, level, std::clamp(follow, 0.0f, 1.0f))
           * std::clamp(intensity, 0.0f, 1.0f);

    // the pars: a peak follower on the hits - up in a few frames, down
    // slowly - so a kick is a swell and a roll of them holds it up rather
    // than flickering on every one
    if (deltaTime > 0.0f)
    {
        const float hit = std::clamp(bus->get(trussChannel, nowSeconds()), 0.0f, 1.0f);
        const float seconds = (hit > trussLevel) ? trussAttack : trussDecay;
        trussLevel = (seconds <= 0.0f)
            ? hit
            : trussLevel + (hit - trussLevel) * (1.0f - std::exp(-deltaTime / seconds));

        if (trigger.fired)
        {
            invert = 1.0f;
        }
        invert = (invertDecay > 0.0f) ? invert * std::exp(-deltaTime / invertDecay) : 0.0f;
    }
}

void Pattern_Mythos_GradientStrobe::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const float up = stageAlpha(node);

    // a triangle rather than a sawtooth, so the gradient runs purple to
    // orange and back with no seam marching up the stage
    const bool truss = isTruss(node);
    const float amount = std::clamp(intensity, 0.0f, 1.0f);

    float place = up * waves + timeActive * speed;
    if (truss && swirl > 0.0f)
    {
        // which way a par is pushed is its own, held still: neighbours
        // part on a hit rather than the whole line sliding as one
        const unsigned int index = static_cast<unsigned int>(node->getStripIdx());
        const float side = hash01(index * 2654435761u + 11u) * 2.0f - 1.0f;
        place += side * swirl * trussLevel * amount;
    }

    // a triangle rather than a sawtooth, so the gradient runs purple to
    // orange and back with no seam marching up the stage
    const float phase = frac(place);
    float mix = 1.0f - std::fabs(phase * 2.0f - 1.0f);
    if (truss)
    {
        mix = std::lerp(mix, 1.0f - mix, std::clamp(trussInvert, 0.0f, 1.0f) * invert * amount);
    }

    // round the short side of the wheel both ways: purple to orange
    // through magenta, and down into the navy through violet, not
    // through the grey an RGB mix of either pair passes
    const ecore::HSV gradient = blendHsv(colorA, colorB, mix);
    inOutColor = blendHsv(gradient, strobeColor, strobe * std::clamp(depth, 0.0f, 1.0f));

    if (truss && trussFloor < 1.0f)
    {
        const float floorValue = std::clamp(trussFloor, 0.0f, 1.0f);
        const float lift = floorValue + (1.0f - floorValue) * std::clamp(trussLevel * amount, 0.0f, 1.0f);
        inOutColor.setBrightnessAlpha(inOutColor.getValFloat() * lift);
    }
}

void Pattern_Mythos_GradientStrobe::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("speed", speed, 0.05f, 3.0f);
    bag.add("waves", waves, 0.25f, 4.0f);
    bag.add("attack", attackSeconds, 0.0f, 0.5f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("decay", decaySeconds, 0.01f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("rate", pulseRate, kQuarterTime, kDoubleTime, [this] { setPulseRate(pulseRate); });
    bag.add("follow", follow, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 4.0f);
    bag.add("slew", slew, 0.0f, 1.0f);
    bag.add("color_a", colorA);
    bag.add("color_b", colorB);
    bag.add("strobe_color", strobeColor);
    bag.add("depth", depth, 0.0f, 1.0f);
    bag.add("truss_floor", trussFloor, 0.0f, 1.0f);
    bag.add("swirl", swirl, 0.0f, 1.0f);
    bag.add("truss_attack", trussAttack, 0.0f, 0.3f);
    bag.add("truss_decay", trussDecay, 0.05f, 1.5f);
    bag.add("truss_invert", trussInvert, 0.0f, 1.0f);
    bag.add("invert_decay", invertDecay, 0.05f, 1.0f);
    bag.add("envelope", envelope.curve);
}

// ============================================================================
// fire and rain, dressed for the show
// ============================================================================

void Pattern_Mythos_Fire::applyIntensity()
{
    // how eagerly the floor ignites: a few embers at the bottom of the knob,
    // the generic look's own default at the top
    sparking = std::lerp(0.12f, 0.47f, std::clamp(intensity, 0.0f, 1.0f));
}

void Pattern_Mythos_Fire::reset()
{
    Pattern_Generic_Fire2012::reset();
    resetMode();
}

void Pattern_Mythos_Fire::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    Coordinate at;
    if (trussRowCoord(node, trussRow, at))
    {
        if (trussBlur <= 0.0f)
        {
            renderAt(at, inOutColor);
            return;
        }
        // a 3x3 patch from the row up, averaged in RGB: the colour of that
        // stretch of fire rather than whichever flame is passing the point
        const float step = trussBlur * 0.5f;
        ecore::HSV mean;
        int taken = 0;
        for (int dy = 0; dy < 3; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                ecore::HSV here;
                renderAt(Coordinate(at.x + dx * step, at.y + dy * step), here);
                ++taken;
                mean = (taken == 1) ? here : blendRgb(mean, here, 1.0f / static_cast<float>(taken));
            }
        }
        inOutColor = mean;
        return;
    }
    Pattern_Generic_Fire2012::render(node, inOutColor);
}

void Pattern_Mythos_Fire::reflect(ecore::PropertyBag& bag)
{
    // first, so they sit where they do on every other cue; `sparking` below
    // is the same number as intensity under its own name, and the last one
    // turned wins
    reflectMode(bag);
    bag.add("intensity", intensity, 0.0f, 1.0f, [this] { applyIntensity(); });
    bag.add("truss_row", trussRow, scanner::kStageBottom, scanner::kStageTop);
    bag.add("truss_blur", trussBlur, 0.0f, 4.0f);
    Pattern_Generic_Fire2012::reflect(bag);
}

Pattern_Mythos_Rain::Pattern_Mythos_Rain()
    : Pattern_Generic_MatrixRain(kDropPool)
{
    hueSpread = 1.0f;   // every drop its own colour
    targetHueSpread = hueSpread;
    applyIntensity();
}

void Pattern_Mythos_Rain::applyIntensity()
{
    // a handful of drops at the bottom of the knob, heavy rain at the top;
    // the downpour modes go past this on their own
    dropCount = std::lerp(8.0f, 48.0f, std::clamp(intensity, 0.0f, 1.0f));
}

void Pattern_Mythos_Rain::reset()
{
    Pattern_Generic_MatrixRain::reset();
    resetMode();

    // a cue opens on its colour: the glide is for a mode change after that,
    // not for a look arriving from wherever the last cue left it
    hueSpread = targetHueSpread;
    white = targetWhite;

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

void Pattern_Mythos_Rain::tick(float deltaTime)
{
    Pattern_Generic_MatrixRain::tick(deltaTime);

    // straight toward the target at a rate that lands in blendSeconds,
    // so green arrives when the blend knob says and not asymptotically
    const float step = (blendSeconds > 0.0f) ? deltaTime / blendSeconds : 1.0f;
    hueSpread += std::clamp(targetHueSpread - hueSpread, -step, step);
    white += std::clamp(targetWhite - white, -step, step);
}

void Pattern_Mythos_Rain::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    Coordinate at;
    if (trussRowCoord(node, trussRow, at))
    {
        renderAt(at, inOutColor);
    }
    else
    {
        Pattern_Generic_MatrixRain::render(node, inOutColor);
    }

    if (white > 0.0f)
    {
        inOutColor.setSaturationAlpha(inOutColor.getSatFloat() * (1.0f - std::clamp(white, 0.0f, 1.0f)));
    }
}

void Pattern_Mythos_Rain::reflect(ecore::PropertyBag& bag)
{
    reflectMode(bag);
    bag.add("intensity", intensity, 0.0f, 1.0f, [this] { applyIntensity(); });
    bag.add("truss_row", trussRow, scanner::kStageBottom, scanner::kStageTop);
    reflectStorm(bag);
    // the colour knobs are the targets - a mode's restore-then-variant
    // writes these, and the drops glide to wherever that lands them
    bag.add("hue_spread", targetHueSpread, 0.0f, 1.0f);
    bag.add("white", targetWhite, 0.0f, 1.0f);
    bag.add("blend", blendSeconds, 0.0f, 4.0f);
}

// ============================================================================
// nova
// ============================================================================

void Pattern_Mythos_Nova::init()
{
    bus = &sharedAudioLevel();
    level = 0.0f;
    hueOffset = 0.0f;
    triggers = &sharedTriggerRack();
    fieldTime = 0.0f;
    pushLevel = 0.0f;
    sinceRing = 1000.0f;
}

void Pattern_Mythos_Nova::reset()
{
    Pattern_MythosLook::reset();
    fieldTime = 0.0f;
    pushLevel = 0.0f;
    sinceRing = 1000.0f;
}

ecore::HSV Pattern_Mythos_Nova::turned(const ecore::HSV& color) const
{
    return turnHue(color, hueOffset);
}

void Pattern_Mythos_Nova::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);
    hueOffset = hueCycle > 0.0f ? frac(hueOffset + deltaTime * hueCycle) : 0.0f;
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }

    // the same slewed read the washes do; the bass presence, because that
    // is what brightens the scene's galaxies
    const float target = std::clamp(bus->get(channel, nowSeconds()) * gain, 0.0f, 1.0f);
    if (slew <= 0.0f || deltaTime <= 0.0f)
    {
        level = target;
    }
    else
    {
        level += (target - level) * (1.0f - std::exp(-deltaTime / slew));
    }

    const double now = nowSeconds();
    const float pushTarget = std::clamp(bus->get(pushChannel, now), 0.0f, 1.0f);
    pushLevel = (pushSlew <= 0.0f || deltaTime <= 0.0f)
        ? pushTarget
        : pushLevel + (pushTarget - pushLevel) * (1.0f - std::exp(-deltaTime / pushSlew));
    fieldTime += std::max(deltaTime, 0.0f) * (1.0f + std::max(push, 0.0f) * pushLevel);

    // the ring leaves the middle on the beat, on the shared trigger so it
    // lands with everything else on the grid
    if (triggers == nullptr)
    {
        triggers = &sharedTriggerRack();
    }
    sinceRing += std::max(deltaTime, 0.0f);
    const BeatTrigger& beat = triggers->forRate(ebeat::kOnBeat);
    if (beat.fired)
    {
        sinceRing = beat.sinceHit;
        const float bpm = sharedBeatClock().getBpm();
        ringSeconds = bpm > 0.0f ? 60.0f / bpm : 0.5f;
    }
}

void Pattern_Mythos_Nova::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const ecore::HSV ground = turned(base);
    const ecore::HSV stars = turned(galaxy);
    const ecore::HSV between = turned(wash);

    // the probe is the galaxies: what the scene paints its own in
    if (isProbe(node))
    {
        inOutColor = stars;
        inOutColor.setBrightnessAlpha(stars.getValFloat());
        return;
    }

    const Coordinate at = nodeCoord(node);
    const float t = fieldTime * 0.06f * speed;
    const float s = scale;

    // the galaxies: one large, slow field, lit only where it peaks, so the
    // rig has a few of them and dark between - the picture is mostly ground
    const float big = scanner::valueNoise(at.x * 0.22f * s + t * 1.3f, at.y * 0.11f * s - t * 0.9f);
    const float swell = 1.0f - std::clamp(follow, 0.0f, 1.0f) * (1.0f - level);
    const float depth = std::clamp(intensity, 0.0f, 1.0f);
    // the threshold falls as the bass presence rises: the galaxies grow
    const float edge = 0.62f - 0.17f * swell * depth;
    const float galaxies = std::clamp((big - edge) / 0.22f, 0.0f, 1.0f);

    // the wash: a finer field in the dark between them, at its amount
    const float fine = scanner::valueNoise(at.x * 0.55f * s - t * 1.7f + 53.0f, at.y * 0.30f * s + t * 1.1f);
    const float washes = std::clamp((fine - 0.45f) / 0.35f, 0.0f, 1.0f)
                       * std::clamp(washAmount, 0.0f, 1.0f) * (1.0f - galaxies);

    // ground, then the wash over it, then the galaxies over both - in RGB,
    // so yellow arriving over dark blue is light on dark and not green
    // the wash on the beat, as far as the presence says: brighter where it
    // is, and spread over the ground between - the ground being the wash
    // dimmed, that is the yellow lighting up
    float beatWash = 0.0f;
    if (washPulse > 0.0f && sinceRing < 30.0f)
    {
        const float rise = std::max(washAttack, 0.001f);
        const float shape = (sinceRing < rise)
            ? sinceRing / rise
            : std::exp(-(sinceRing - rise) / std::max(washDecay, 0.01f));
        beatWash = std::clamp(washPulse * shape * level * depth, 0.0f, 1.0f);
    }
    ecore::HSV out = blendRgb(ground, between, washes + (1.0f - washes) * beatWash * 0.6f);
    out = blendRgb(out, stars, galaxies);
    inOutColor = out;
    float value = out.getValFloat() * (0.85f + 0.15f * swell);
    value += (1.0f - value) * beatWash * washes * (1.0f - galaxies);

    // the beat ring: out from the middle to both ends over the beat, a
    // soft band lifting what it crosses and fading as it reaches the ends
    if (pulse > 0.0f && sinceRing < ringSeconds)
    {
        const float reach = sinceRing / std::max(ringSeconds, 0.05f);
        const float from = std::fabs(stageAlpha(node) - 0.5f) * 2.0f;
        const bool truss = isTruss(node);
        const float width = (truss && trussRing >= 0.0f) ? trussRing : ring;
        const float off = (from - reach) / std::max(width, 0.02f);
        // in over the first of the beat, out over the last, so one ring
        // hands to the next through dark rather than with a cut at the ends
        // and a pop in the middle
        const float easeIn = (truss && trussEase > 0.0f) ? trussEase : ease;
        const float in = easeIn > 0.0f ? std::clamp(reach / easeIn, 0.0f, 1.0f) : 1.0f;
        const float out = std::clamp((1.0f - reach) / 0.3f, 0.0f, 1.0f);
        const float band = std::exp(-off * off) * (1.0f - 0.6f * reach)
                         * in * in * (3.0f - 2.0f * in)
                         * out * out * (3.0f - 2.0f * out);
        value += (1.0f - value) * std::clamp(pulse * band * depth, 0.0f, 1.0f);
    }
    inOutColor.setBrightnessAlpha(value);
}

void Pattern_Mythos_Nova::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("speed", speed, 0.1f, 4.0f);
    bag.add("scale", scale, 0.3f, 3.0f);
    bag.add("wash_amount", washAmount, 0.0f, 1.0f);
    bag.add("follow", follow, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 4.0f);
    bag.add("slew", slew, 0.0f, 1.0f);
    bag.add("base", base);
    bag.add("galaxy", galaxy);
    bag.add("wash", wash);
    bag.add("hue_cycle", hueCycle, 0.0f, 1.0f);
    bag.add("push", push, 0.0f, 3.0f);
    bag.add("push_slew", pushSlew, 0.0f, 1.0f);
    bag.add("pulse", pulse, 0.0f, 1.0f);
    bag.add("ring", ring, 0.02f, 0.5f);
    bag.add("ease", ease, 0.0f, 0.5f);
    bag.add("truss_ring", trussRing, -1.0f, 0.6f);
    bag.add("truss_ease", trussEase, 0.0f, 0.5f);
    bag.add("wash_pulse", washPulse, 0.0f, 1.0f);
    bag.add("wash_attack", washAttack, 0.0f, 0.3f);
    bag.add("wash_decay", washDecay, 0.05f, 1.0f);
}

// ============================================================================
// cloud flight
// ============================================================================

namespace
{
    /// The deck's loop, in lattice cells of its two octaves: the coarse
    /// field wraps every kDeckLoopCoarse cells and the fine one - at two
    /// and a half times the frequency - every kDeckLoopFine, so one turn
    /// of `travelled` is a whole number of cells on both and the deck
    /// comes round on itself. A loop this long is half a minute at the
    /// cue's speed.
    constexpr int kDeckLoopCoarse = 16;
    constexpr int kDeckLoopFine = 40;
    /// Turns of the loop per second per unit of speed - what puts the
    /// deck's streaming at the rate it had.
    constexpr float kDeckLoopRate = 0.1125f;
}

Pattern_Mythos_CloudFlight::Pattern_Mythos_CloudFlight()
{
    // the speed is the pads': a mode is an altitude and must not put the
    // throttle back where the cue opened. The height is the modes' own.
    keepAcrossModes({"speed"});
}

void Pattern_Mythos_CloudFlight::init()
{
    heightNow = height;
    speedNow = speed;
    travelled = 0.0f;
}

void Pattern_Mythos_CloudFlight::reset()
{
    Pattern_MythosLook::reset();
    // a cue entered is a flight begun where its knobs say, not gliding in
    // from wherever the last visit left it
    heightNow = height;
    speedNow = speed;
    travelled = 0.0f;
}

void Pattern_Mythos_CloudFlight::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);

    // both glide: a pad moves the target and the flight takes `glide`
    // seconds to get there, the way a plane climbs rather than teleports
    if (glide <= 0.0f || deltaTime <= 0.0f)
    {
        heightNow = height;
        speedNow = speed;
    }
    else
    {
        const float k = 1.0f - std::exp(-deltaTime * 3.0f / glide);
        heightNow += (height - heightNow) * k;
        speedNow += (speed - speedNow) * k;
    }
    // the deck streams at the speed the flight is actually doing, round a
    // loop the field is periodic on - see travelled
    travelled = frac(travelled + deltaTime * speedNow * kDeckLoopRate);
}

void Pattern_Mythos_CloudFlight::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    if (trussLow >= 0.0f && isTruss(node))
    {
        // the whole truss one light: the sky toward yellow the higher the
        // flight, dim in the deck and bright above it
        const float h = std::clamp(heightNow, 0.0f, 1.0f);
        const float level = std::lerp(std::clamp(trussLow, 0.0f, 1.0f),
                                 std::clamp(trussHigh, 0.0f, 1.0f), h);
        // a sunset along the line of them, the whole of it warming toward
        // the yellow as the flight climbs
        const eio::HSVStripNode_Space* spaced = eio::spaceOf(node);
        const float along = (spaced != nullptr) ? spaced->u - 0.5f : 0.0f;
        const float warm = std::clamp(h * 0.6f + along * std::clamp(trussSpread, 0.0f, 1.0f)
                                      + 0.5f * std::clamp(trussSpread, 0.0f, 1.0f) * (1.0f - h), 0.0f, 1.0f);
        ecore::HSV light = blendRgb(skyLow, skyHigh, warm);

        // and the clouds going over it: the deck's field at the par,
        // streaming as the deck does - thin cloud a shadow, a peak pale
        const float passing = std::clamp(trussClouds, 0.0f, 1.0f) * std::clamp(intensity, 0.0f, 1.0f);
        float shade = 1.0f;
        if (passing > 0.0f)
        {
            const Coordinate at = nodeCoord(node);
            const float n = 0.6f * scanner::valueNoiseLoop(at.x * 0.35f + 11.0f,
                                                           at.y * 0.18f + travelled * kDeckLoopCoarse,
                                                           kDeckLoopCoarse)
                          + 0.4f * scanner::valueNoiseLoop(at.x * 0.80f - 7.0f,
                                                           at.y * 0.45f + travelled * kDeckLoopFine,
                                                           kDeckLoopFine);
            shade = 1.0f - passing * std::clamp((0.55f - n) / 0.35f, 0.0f, 1.0f);
            ecore::HSV pale(skyLow.getHueFloat(), skyLow.getSatFloat() * 0.35f, 1.0f);
            pale.setBrightnessAlpha(1.0f);
            light = blendRgb(light, pale, passing * 0.6f * std::clamp((n - 0.6f) / 0.25f, 0.0f, 1.0f));
        }
        inOutColor = light;
        inOutColor.setBrightnessAlpha(level * shade);
        return;
    }

    const float up = stageAlpha(node);
    const Coordinate at = nodeCoord(node);

    // the horizon: where the flight's height puts it, 1 - height because a
    // high flight looks down on a deck that has fallen away to the bottom
    const float horizon = 1.0f - std::clamp(heightNow, 0.0f, 1.0f);

    const float amount = std::clamp(clouds, 0.0f, 1.0f);

    // the sky: yellow overhead into pink at the horizon
    const float skyT = horizon >= 0.999f ? 0.0f : std::clamp((up - horizon) / (1.0f - horizon), 0.0f, 1.0f);
    ecore::HSV sky = blendRgb(skyLow, skyHigh, skyT);

    // and the wisps in it: a third field on the deck's loop, streaming with
    // the flight, lit as pale cloud where it peaks - so the sky has cloud
    // in it too, and a high flight is not a rig gone to plain gradient
    const float w = scanner::valueNoiseLoop(at.x * 0.55f + 23.0f,
                                            at.y * 0.30f + travelled * kDeckLoopCoarse,
                                            kDeckLoopCoarse);
    const float wisp = std::clamp((w - 0.55f) / 0.30f, 0.0f, 1.0f) * amount
                     * std::clamp(intensity, 0.0f, 1.0f);
    ecore::HSV pale(skyLow.getHueFloat(), skyLow.getSatFloat() * 0.35f, 1.0f);
    pale.setBrightnessAlpha(1.0f);
    sky = blendRgb(sky, pale, wisp * 0.7f);

    // the deck: the cloud's own noise, streaming down the stage, each
    // octave scrolled by its own whole number of cells per turn
    const float n = 0.6f * scanner::valueNoiseLoop(at.x * 0.35f + 11.0f,
                                                   at.y * 0.18f + travelled * kDeckLoopCoarse,
                                                   kDeckLoopCoarse)
                  + 0.4f * scanner::valueNoiseLoop(at.x * 0.80f - 7.0f,
                                                   at.y * 0.45f + travelled * kDeckLoopFine,
                                                   kDeckLoopFine);
    // the tops catch the sunset: the pink, at `glow`, where the deck peaks
    // - and more of the deck peaks the more cloud there is
    const float crest = 0.60f - 0.30f * amount;
    const float lit = std::clamp((n - crest) / 0.3f, 0.0f, 1.0f) * std::clamp(glow, 0.0f, 1.0f)
                    * std::clamp(intensity, 0.0f, 1.0f);
    ecore::HSV deck = blendRgb(cloud, skyLow, lit);
    deck.setBrightnessAlpha(std::lerp(cloud.getValFloat() * (0.6f + 0.4f * n), skyLow.getValFloat(), lit));

    // the deck's edge is soft - a band of the stage's height either side
    // of the horizon where cloud and sky mix - so the pars do not step
    const float edge = std::clamp((up - horizon + 0.06f) / 0.12f, 0.0f, 1.0f);
    inOutColor = blendRgb(deck, sky, edge);
}

void Pattern_Mythos_CloudFlight::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("height", height, 0.0f, 1.0f);
    bag.add("speed", speed, 0.0f, 2.0f);
    bag.add("glide", glide, 0.0f, 10.0f);
    bag.add("glow", glow, 0.0f, 1.0f);
    bag.add("clouds", clouds, 0.0f, 1.0f);
    bag.add("sky_high", skyHigh);
    bag.add("sky_low", skyLow);
    bag.add("cloud", cloud);
    bag.add("truss_low", trussLow, -1.0f, 1.0f);
    bag.add("truss_high", trussHigh, 0.0f, 1.0f);
    bag.add("truss_clouds", trussClouds, 0.0f, 1.0f);
    bag.add("truss_spread", trussSpread, 0.0f, 1.0f);
}

// ============================================================================
// reaction
// ============================================================================

namespace
{
    /// Where a node is across the stage, 0..1. The truss reads its own run
    /// - the pars stand at one x beside the sculpture, and a rainbow keyed
    /// to that would put one hue on all ten - so a par is its place along
    /// the truss, and everything else is its x over the stage's columns.
    float stageAcross(const eio::HSVStripNode* node)
    {
        const eio::HSVStripNode_Space* spaced = eio::spaceOf(node);
        if (spaced != nullptr && spaced->space == eio::NodeSpace::Truss)
        {
            return spaced->u;
        }
        if (node != nullptr && node->GetStripNodeType() == eio::StripNodeType::MAPPED2D)
        {
            const float x = static_cast<const eio::HSVStripNode_Mapped2D*>(node)->coord.x;
            return std::clamp(x / static_cast<float>(scanner::kStageColumns - 1), 0.0f, 1.0f);
        }
        return 0.5f;
    }

    /// The slewed read every cue on the bus does, so they breathe alike.
    void slewToward(float& value, float target, float seconds, float deltaTime)
    {
        if (seconds <= 0.0f || deltaTime <= 0.0f)
        {
            value = target;
        }
        else
        {
            value += (target - value) * (1.0f - std::exp(-deltaTime / seconds));
        }
    }

    float smoothstep(float edge0, float edge1, float x)
    {
        const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

void Pattern_Mythos_Reaction::init()
{
    bus = &sharedAudioLevel();
    level = 0.0f;
}

void Pattern_Mythos_Reaction::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }
    const float target = std::clamp(bus->get(channel, nowSeconds()) * gain, 0.0f, 1.0f);
    slewToward(level, target, slew, deltaTime);
}

float Pattern_Mythos_Reaction::getRainbow() const
{
    return smoothstep(threshold, threshold + std::max(knee, 0.0001f), level)
         * std::clamp(intensity, 0.0f, 1.0f);
}

void Pattern_Mythos_Reaction::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    const float turn = timeActive * hueRate;

    if (isProbe(node))
    {
        inOutColor = HSV(frac(0.5f * span + turn) * 360.0f, 1.0f, 1.0f);
        return;
    }

    // the rainbow: the wheel across the stage, turning
    const float across = stageAcross(node);
    const HSV rainbow(frac(across * span + turn) * 360.0f, 1.0f, 1.0f);

    // the white: a fine grain drifting, at the floor and never above it
    const Coordinate at = nodeCoord(node);
    const float t = timeActive * 0.4f;
    const float n = scanner::valueNoise(at.x * 0.9f + t * 1.3f, at.y * 0.6f - t * 0.9f);
    const float floorValue = std::clamp(floorLevel, 0.0f, 1.0f);
    const float whiteValue = floorValue * (1.0f - std::clamp(grain, 0.0f, 1.0f) * (1.0f - n));
    const HSV white(0.0f, 0.0f, whiteValue);

    // the presence decides: white comes through as the rainbow comes up,
    // mixed as light so the hues arrive tinted rather than through grey
    const float amount = getRainbow();
    inOutColor = blendRgb(white, rainbow, amount);
    inOutColor.setBrightnessAlpha(std::lerp(whiteValue, 1.0f, amount));
}

void Pattern_Mythos_Reaction::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("grain", grain, 0.0f, 1.0f);
    bag.add("span", span, 0.0f, 3.0f);
    bag.add("hue_rate", hueRate, 0.0f, 1.0f);
    bag.add("threshold", threshold, 0.0f, 1.0f);
    bag.add("knee", knee, 0.01f, 1.0f);
    bag.add("gain", gain, 0.0f, 4.0f);
    bag.add("slew", slew, 0.0f, 2.0f);
}

// ============================================================================
// scaffold
// ============================================================================

Pattern_Mythos_Scaffold::Pattern_Mythos_Scaffold()
    : triggers(&sharedTriggerRack())
{
    // RestartHold, like beat_pulse: a beat landing in the fall lifts the
    // pattern back out from wherever it got to rather than snapping it to
    // nothing first.
    envelope.retriggerMode = eanim::RetriggerMode::RestartHold;
    setEnvelope(attackSeconds, decaySeconds);
}

void Pattern_Mythos_Scaffold::setEnvelope(float inAttackSeconds, float inDecaySeconds)
{
    attackSeconds = std::max(inAttackSeconds, 0.0f);
    decaySeconds  = std::max(inDecaySeconds, 0.001f);

    envelope.curve.clear();
    envelope.curve.addKey(0.0f, 0.0f);
    envelope.curve.addKey(attackSeconds, 1.0f, easing_functions::EaseOutCubic);
    envelope.curve.addKey(attackSeconds + decaySeconds, 0.0f);
}

void Pattern_Mythos_Scaffold::setPulseRate(float pulsesPerBeat)
{
    pulseRate = snapPulseRate(pulsesPerBeat);
}

void Pattern_Mythos_Scaffold::reset()
{
    Pattern_MythosLook::reset();
    envelope.reset();
    pulse = 0.0f;
    travel = 0.0;
}

void Pattern_Mythos_Scaffold::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);

    const BeatTrigger& trigger = triggers->forRate(pulseRate);
    if (trigger.fired)
    {
        envelope.triggerAt(trigger.sinceHit);
    }
    envelope.tick(deltaTime);

    pulse = std::clamp(envelope.getValue(), 0.0f, 1.0f) * std::clamp(intensity, 0.0f, 1.0f);

    travel = sharedBeatClock().beatPosition(nowSeconds()) * static_cast<double>(strutRate);
}

void Pattern_Mythos_Scaffold::render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const
{
    if (isProbe(node))
    {
        inOutColor = glint;
        inOutColor.setBrightnessAlpha(glint.getValFloat());
        return;
    }

    const Coordinate at = nodeCoord(node);

    // the fog: the ground, breathing a little in a slow field so the dark
    // between struts is a space and not a flat colour
    const float tf = timeActive * 0.15f;
    const float fog = scanner::valueNoise(at.x * 0.4f + tf + 11.0f, at.y * 0.25f - tf * 0.6f);
    const float groundValue = ground.getValFloat() * (0.7f + 0.6f * fog);

    // the struts: bands across the stage, `struts` of them on its height,
    // sweeping down as the beat clock runs. `place` counts struts from the
    // floor plus the travel, so its integer part names the strut a node is
    // in and every other one is ember.
    const double place = static_cast<double>(stageAlpha(node) * std::max(struts, 0.25f)) + travel;
    const double which = std::floor(place);
    const float across = static_cast<float>(place - which);
    const float half = std::clamp(strutWidth, 0.02f, 1.0f) * 0.5f;
    const float fromCentre = std::fabs(across - 0.5f);
    // A run is a pixel and takes a crisp line. A par is a lamp lighting the
    // room, and a line crossing it in a tenth of a second is a blink - so
    // the pars swell and fade over the whole gap, one after another up the
    // truss, which is the line passing them rather than flashing them.
    const float band = isTruss(node)
        ? std::pow(0.5f + 0.5f * std::cos(fromCentre * 2.0f * 3.14159265f), 2.0f)
        : 1.0f - smoothstep(half * 0.5f, half, fromCentre);

    const bool isEmber = (static_cast<long long>(which) & 1LL) != 0;
    const HSV& strut = isEmber ? ember : glint;

    const float amount = std::clamp(intensity, 0.0f, 1.0f);
    const float lifted = 1.0f - std::clamp(lift, 0.0f, 1.0f) * (1.0f - pulse);
    const float strutValue = strut.getValFloat() * lifted * amount;

    const float mix = band * amount;
    inOutColor = blendRgb(ground, strut, mix);
    inOutColor.setBrightnessAlpha(std::lerp(groundValue, std::max(strutValue, groundValue), band));
}

void Pattern_Mythos_Scaffold::reflect(ecore::PropertyBag& bag)
{
    Pattern_MythosLook::reflect(bag);
    bag.add("strut_rate", strutRate, 0.0f, 4.0f);
    bag.add("struts", struts, 0.5f, 8.0f);
    bag.add("strut_width", strutWidth, 0.05f, 1.0f);
    bag.add("lift", lift, 0.0f, 1.0f);
    bag.add("attack", attackSeconds, 0.0f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("decay", decaySeconds, 0.01f, 3.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
    bag.add("rate", pulseRate, kQuarterTime, kDoubleTime, [this] { setPulseRate(pulseRate); });
    bag.add("ground", ground);
    bag.add("glint", glint);
    bag.add("ember", ember);
    bag.add("envelope", envelope.curve);
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
