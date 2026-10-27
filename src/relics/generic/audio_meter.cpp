// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "audio_meter.h"

#include <algorithm>
#include <cmath>

using namespace generic;

// ============================================================================
// Pattern_Audio_Meter
// ============================================================================

void Pattern_Audio_Meter::init()
{
    meter = &sharedAudioLevel();
    value = 0.0f;
    peak = 0.0f;
    live = false;
    clock = 0.0f;
}

void Pattern_Audio_Meter::tick(float deltaTime)
{
    clock += deltaTime;

    if (meter == nullptr)
    {
        meter = &sharedAudioLevel();
    }

    const double now = nowSeconds();
    live = meter->isLive(channel, now);
    value = meter->get(channel, now);

    // The peak rises instantly and falls slowly. Instantly because the whole
    // reason it is here is a transient two frames wide - a peak that had to be
    // chased would miss exactly what it is for.
    if (value >= peak || peakDecay <= 0.0f || deltaTime <= 0.0f)
    {
        peak = value;
        return;
    }

    // Framerate-independent, so the meter reads the same on a laptop dropping
    // frames as on the desk. Falls *to the live value* rather than to zero:
    // this is a marker above the reading, not a second reading.
    const float alpha = 1.0f - std::exp(-deltaTime / peakDecay);
    peak += (value - peak) * alpha;
}

void Pattern_Audio_Meter::render(eio::HSVStripNode* /*node*/,
                                 ecore::HSV& inOutColor) const
{
    // Deliberately identical on every fixture; see the note in the header on
    // why this is not a spatial meter.
    if (!live)
    {
        // A channel nobody is filling, said as something no reading looks
        // like: a slow amber pulse, dim, that never reaches the brightness a
        // real value would. Zero and "not wired" are the two states this look
        // exists to tell apart, and drawing both as black would have made it
        // useless for the one job it has.
        const float breath = 0.5f + (0.5f * std::sin(clock * 3.0f));
        inOutColor = ecore::HSV(38.0f, 1.0f, 0.05f + (0.07f * breath));
        return;
    }

    const float shown = std::max(value, peak * peakMix);
    const float lit = std::clamp(floorLevel + (shown * gain), 0.0f, 1.0f);

    inOutColor = color;
    inOutColor.setBrightnessAlpha(color.getValFloat() * lit);
}

void Pattern_Audio_Meter::reflect(ecore::PropertyBag& bag)
{
    bag.add("color", color);
    bag.add("gain", gain, 0.0f, 4.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("peak_decay", peakDecay, 0.0f, 4.0f);
    bag.add("peak_mix", peakMix, 0.0f, 1.0f);

    // Read-only in spirit: the bus overwrites both every frame, so writing one
    // lasts until the next tick. Registered as knobs anyway, not as state -
    // state is a clock for the sim handshake and never shown - because
    // `params` is how a desk reads a look's numbers, and "what is this
    // channel doing right now" is the single most useful number this look has.
    bag.add("value", value, 0.0f, 1.0f);
    bag.add("peak", peak, 0.0f, 1.0f);
}
