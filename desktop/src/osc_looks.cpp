// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/osc_looks.h"

#include <algorithm>
#include <cmath>

#include "edmx/color_mix.h"

using namespace edmx;

namespace
{
    float glide(float current, float target, float seconds, float deltaTime)
    {
        if (seconds <= 0.0f || deltaTime <= 0.0f)
        {
            return target;
        }
        return current + (target - current) * (1.0f - std::exp(-deltaTime / seconds));
    }

    float smooth(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /// the origin line as a share of the stage: where the ring sits, and
    /// where a beat starts from
    constexpr float kOriginAlpha =
        (scanner::kStageOriginY - scanner::kStageBottom) / (scanner::kStageTop - scanner::kStageBottom);
}

// ============================================================================
// the shared part
// ============================================================================

OscHost& edmx::sharedOscHost()
{
    static OscHost host;
    return host;
}

void Pattern_Osc_Look::reset()
{
    PatternScanner::reset();
    // the levels are not reset: they are the sender's, and a look coming up
    // mid-song should come up at the song's level, not climb to it
    sinceBeat = 1000.0f;
    beats = 0;
    // the tick standing in the knob is the last one heard, not a new beat
    lastTick = host.tick;
    sinceTick = 1000.0f;
    downbeat = false;
}

void Pattern_Osc_Look::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }
    const float dt = std::max(deltaTime, 0.0f);

    const double now = nowSeconds();
    const float scale = std::max(gain, 0.0f);
    const auto read = [&](AudioChannel channel) { return std::clamp(bus->get(channel, now) * scale, 0.0f, 1.0f); };
    level = glide(level, read(AudioChannel::Level), slew, dt);
    bass = glide(bass, read(AudioChannel::Bass), slew, dt);
    mid = glide(mid, read(AudioChannel::Mid), slew, dt);
    high = glide(high, read(AudioChannel::High), slew, dt);
    kick = read(AudioChannel::BassHits);
    onset = read(AudioChannel::Hits);
    // slower than the bands, so a hat between kicks tints rather than flickers
    const float bands = bass + high;
    timbre = glide(timbre, bands > 0.02f ? high / bands : 0.5f, 0.4f, dt);

    // the last tempo heard stands: a stale bpm reads as zero, and zero is
    // not a tempo, it is a sender that went quiet
    const float bpmLevel = bus->get(AudioChannel::Bpm, now);
    if (bpmLevel > 0.0f)
    {
        bpm = 50.0f + bpmLevel * 170.0f;
    }

    // a beat is the rising edge, with hysteresis so a spike that wobbles
    // across the line is one beat and not three
    sinceBeat += dt;
    const float beat = bus->get(AudioChannel::Beat, now);
    if (!beatHigh && beat > 0.5f)
    {
        beatHigh = true;
        sinceBeat = 0.0f;
        ++beats;
    }
    else if (beatHigh && beat < 0.25f)
    {
        beatHigh = false;
    }

    // the host's tick says which beat this is: the one when its count is a
    // multiple of four. It lands within a frame of the beat's edge, so the
    // accent it sets is on the ring that beat starts. A host that stops
    // ticking (it lost the beat) leaves every beat the same.
    sinceTick += dt;
    if (host.tick != lastTick)
    {
        lastTick = host.tick;
        sinceTick = 0.0f;
        downbeat = (static_cast<long>(std::lround(host.tick)) % 4) == 0;
    }
    if (sinceTick > 3.0f)
    {
        downbeat = false;
    }

    // the cut is taken by whichever look sees it first; the time is shared,
    // so both looks in a blend light it together
    if (host.cut > 0.5f)
    {
        host.cut = 0.0f;
        host.cutAt = now;
    }
    const float sinceCut = static_cast<float>(now - host.cutAt);
    cutGlow = (sinceCut >= 0.0f && sinceCut < cutSeconds) ? 1.0f - smooth(sinceCut / std::max(cutSeconds, 0.01f)) : 0.0f;
}

float Pattern_Osc_Look::beatPhase() const
{
    const float period = 60.0f / std::max(bpm, 1.0f);
    return std::clamp(sinceBeat / period, 0.0f, 1.0f);
}

float Pattern_Osc_Look::accent() const
{
    // with the tick arriving, the one is full and the rest sit under it;
    // without, every beat is full
    return (sinceTick > 3.0f || downbeat) ? 1.0f : 0.7f;
}

float Pattern_Osc_Look::finish(float value) const
{
    const float screen = std::clamp(std::max(host.brightA, host.brightB), 0.0f, 1.0f);
    value *= 1.0f - std::clamp(follow, 0.0f, 1.0f) * (1.0f - screen);
    value += (1.0f - value) * std::clamp(cutLift, 0.0f, 1.0f) * cutGlow;
    return std::clamp(value, 0.0f, 1.0f);
}

float Pattern_Osc_Look::balance() const
{
    const float a = std::max(host.amountA, 0.0f);
    const float b = std::max(host.amountB, 0.0f);
    // kept off the ends, so neither colour leaves the stage entirely
    return (a + b) > 0.02f ? std::clamp(a / (a + b), 0.15f, 0.85f) : 0.5f;
}

void Pattern_Osc_Look::reflect(ecore::PropertyBag& bag)
{
    bag.add("hue_a", host.hueA, 0.0f, 360.0f);
    bag.add("sat_a", host.satA, 0.0f, 1.0f);
    bag.add("hue_b", host.hueB, 0.0f, 360.0f);
    bag.add("sat_b", host.satB, 0.0f, 1.0f);
    bag.add("bright_a", host.brightA, 0.0f, 1.0f);
    bag.add("bright_b", host.brightB, 0.0f, 1.0f);
    bag.add("amount_a", host.amountA, 0.0f, 1.0f);
    bag.add("amount_b", host.amountB, 0.0f, 1.0f);
    bag.add("tick", host.tick, 0.0f, 16000000.0f);
    bag.add("cut", host.cut, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 2.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("slew", slew, 0.0f, 1.0f);
    bag.add("follow", follow, 0.0f, 1.0f);
    bag.add("cut_lift", cutLift, 0.0f, 1.0f);
    bag.add("cut_seconds", cutSeconds, 0.05f, 4.0f);
}

// ============================================================================
// osc_wash
// ============================================================================

void Pattern_Osc_Wash::reset()
{
    Pattern_Osc_Look::reset();
    roll = 0.0f;
}

void Pattern_Osc_Wash::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    // one stage every four beats on the sender's tempo, up to half as fast
    // again with the mids (a voice, a pad) up
    roll = std::fmod(roll + std::max(deltaTime, 0.0f) * bpm / 240.0f * (0.75f + 0.75f * mid), 1.0f);
    time = std::fmod(time + std::max(deltaTime, 0.0f), 3600.0f);
}

void Pattern_Osc_Wash::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    // a triangle wave up the stage, so the gradient rolls without a seam.
    // Its values are spread evenly over the stage, so moving the crossover
    // to the primary's share of the screen gives it that share of the stage
    const float along = std::fmod(height - roll + 1.0f, 1.0f);
    const float tri = 1.0f - std::fabs(along * 2.0f - 1.0f);
    const float mix = smooth((tri - balance()) * 2.0f + 0.5f);

    float value = floorLevel + (1.0f - floorLevel) * level;
    value += (1.0f - value) * kick * flash;

    // the highs as glitter: a few points up the stage, crawling, lit by the
    // hats and gone in a bassline
    const float sparkle = std::pow(0.5f + 0.5f * std::sin(height * 61.0f + time * 7.0f), 6.0f);
    value += (1.0f - value) * glitter * high * sparkle;

    inOutColor = blendRgb(primary(1.0f), secondary(1.0f), mix);
    inOutColor.setBrightnessAlpha(finish(value));
}

void Pattern_Osc_Wash::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("flash", flash, 0.0f, 1.0f);
    bag.add("glitter", glitter, 0.0f, 1.0f);
}

// ============================================================================
// osc_pulse
// ============================================================================

void Pattern_Osc_Pulse::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    const float bedValue = floorLevel + (1.0f - floorLevel) * std::clamp(level * bed + onset * flick, 0.0f, 1.0f);

    // the ring leaves the origin both ways and reaches the far end of the
    // stage as the beat ends, fading as it goes so the next one starts clean
    const float phase = beatPhase();
    const float reach = phase * std::max(kOriginAlpha, 1.0f - kOriginAlpha);
    const float width = std::max(ring * (1.0f + bass), 0.02f);
    const float off = (std::fabs(height - kOriginAlpha) - reach) / width;
    const float band = std::exp(-off * off) * (1.0f - smooth(phase)) * accent();

    inOutColor = blendRgb(secondary(1.0f), primary(1.0f), band);
    inOutColor.setBrightnessAlpha(finish(bedValue + (1.0f - bedValue) * band));
}

void Pattern_Osc_Pulse::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("ring", ring, 0.02f, 0.5f);
    bag.add("bed", bed, 0.0f, 1.0f);
    bag.add("flick", flick, 0.0f, 1.0f);
}

// ============================================================================
// osc_meter
// ============================================================================

void Pattern_Osc_Meter::reset()
{
    Pattern_Osc_Look::reset();
    peak = 0.0f;
}

void Pattern_Osc_Meter::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    peak = std::max(level, peak - std::max(fall, 0.0f) * std::max(deltaTime, 0.0f));
}

void Pattern_Osc_Meter::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    // a pixel's worth of stage, so the bar's head is soft rather than a step
    constexpr float kEdge = 0.03f;

    const float lit = 1.0f - smooth((height - level) / kEdge);
    // up the bar from primary toward secondary, the head going all the way
    // only on a bright sound
    const float headMix = level > 0.0f ? std::clamp(height / level, 0.0f, 1.0f) * (0.35f + 0.65f * timbre) : 0.0f;
    const float cap = std::exp(-std::pow((height - peak) / kEdge, 2.0f));
    const float capValue = 0.5f + 0.5f * (1.0f - beatPhase()) * accent();

    ecore::HSV bar = blendRgb(primary(1.0f), secondary(1.0f), headMix);
    ecore::HSV out = blendRgb(secondary(1.0f), bar, lit);
    out = blendRgb(out, secondary(1.0f), cap * (1.0f - lit));

    const float barValue = lit * (1.0f - punch + punch * kick);
    const float value = std::max({floorLevel * (1.0f - lit), barValue, cap * capValue});
    out.setBrightnessAlpha(finish(value));
    inOutColor = out;
}

void Pattern_Osc_Meter::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("fall", fall, 0.0f, 2.0f);
    bag.add("punch", punch, 0.0f, 1.0f);
}
