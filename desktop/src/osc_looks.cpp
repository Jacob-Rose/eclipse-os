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

OscColours& edmx::sharedOscColours()
{
    static OscColours colours;
    return colours;
}

void Pattern_Osc_Look::reset()
{
    PatternScanner::reset();
    // the levels are not reset: they are the sender's, and a look coming up
    // mid-song should come up at the song's level, not climb to it
    sinceBeat = 1000.0f;
    beats = 0;
}

void Pattern_Osc_Look::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }

    const double now = nowSeconds();
    const float scale = std::max(gain, 0.0f);
    instant = std::clamp(bus->get(AudioChannel::LevelInstant, now) * scale, 0.0f, 1.0f);
    average = glide(average, std::clamp(bus->get(AudioChannel::LevelAverage, now) * scale, 0.0f, 1.0f), slew, deltaTime);
    meter = glide(meter, std::clamp(bus->get(AudioChannel::LevelMeter, now) * scale, 0.0f, 1.0f), slew, deltaTime);

    // the last tempo heard stands: a stale bpm reads as zero, and zero is
    // not a tempo, it is a sender that went quiet
    const float bpmLevel = bus->get(AudioChannel::Bpm, now);
    if (bpmLevel > 0.0f)
    {
        bpm = 50.0f + bpmLevel * 170.0f;
    }

    // a beat is the rising edge, with hysteresis so a spike that wobbles
    // across the line is one beat and not three
    sinceBeat += std::max(deltaTime, 0.0f);
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
}

float Pattern_Osc_Look::beatPhase() const
{
    const float period = 60.0f / std::max(bpm, 1.0f);
    return std::clamp(sinceBeat / period, 0.0f, 1.0f);
}

void Pattern_Osc_Look::reflect(ecore::PropertyBag& bag)
{
    bag.add("hue_a", colours.hueA, 0.0f, 360.0f);
    bag.add("sat_a", colours.satA, 0.0f, 1.0f);
    bag.add("hue_b", colours.hueB, 0.0f, 360.0f);
    bag.add("sat_b", colours.satB, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 2.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("slew", slew, 0.0f, 1.0f);
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
    // one stage every four beats, on the sender's tempo
    roll = std::fmod(roll + std::max(deltaTime, 0.0f) * bpm / 240.0f, 1.0f);
}

void Pattern_Osc_Wash::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    // a triangle wave up the stage, so the gradient rolls without a seam
    const float along = std::fmod(stageAlpha(inNode) - roll + 1.0f, 1.0f);
    const float mix = 1.0f - std::fabs(along * 2.0f - 1.0f);

    float value = floorLevel + (1.0f - floorLevel) * average;
    value += (1.0f - value) * instant * flash;

    inOutColor = blendRgb(primary(1.0f), secondary(1.0f), smooth(mix));
    inOutColor.setBrightnessAlpha(std::clamp(value, 0.0f, 1.0f));
}

void Pattern_Osc_Wash::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("flash", flash, 0.0f, 1.0f);
}

// ============================================================================
// osc_pulse
// ============================================================================

void Pattern_Osc_Pulse::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    const float bedValue = floorLevel + (1.0f - floorLevel) * average * bed;

    // the ring leaves the origin both ways and reaches the far end of the
    // stage as the beat ends, fading as it goes so the next one starts clean
    const float phase = beatPhase();
    const float reach = phase * std::max(kOriginAlpha, 1.0f - kOriginAlpha);
    const float off = (std::fabs(height - kOriginAlpha) - reach) / std::max(ring, 0.02f);
    const float band = std::exp(-off * off) * (1.0f - smooth(phase));

    inOutColor = blendRgb(secondary(1.0f), primary(1.0f), band);
    inOutColor.setBrightnessAlpha(std::clamp(bedValue + (1.0f - bedValue) * band, 0.0f, 1.0f));
}

void Pattern_Osc_Pulse::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("ring", ring, 0.02f, 0.5f);
    bag.add("bed", bed, 0.0f, 1.0f);
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
    peak = std::max(meter, peak - std::max(fall, 0.0f) * std::max(deltaTime, 0.0f));
}

void Pattern_Osc_Meter::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    // a pixel's worth of stage, so the bar's head is soft rather than a step
    constexpr float kEdge = 0.03f;

    const float lit = 1.0f - smooth((height - meter) / kEdge);
    const float headMix = meter > 0.0f ? std::clamp(height / meter, 0.0f, 1.0f) : 0.0f;
    const float cap = std::exp(-std::pow((height - peak) / kEdge, 2.0f));
    const float capValue = 0.5f + 0.5f * (1.0f - beatPhase());

    ecore::HSV bar = blendRgb(primary(1.0f), secondary(1.0f), headMix);
    ecore::HSV out = blendRgb(secondary(1.0f), bar, lit);
    out = blendRgb(out, secondary(1.0f), cap * (1.0f - lit));

    const float value = std::max({floorLevel * (1.0f - lit), lit, cap * capValue});
    out.setBrightnessAlpha(std::clamp(value, 0.0f, 1.0f));
    inOutColor = out;
}

void Pattern_Osc_Meter::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("fall", fall, 0.0f, 2.0f);
}
