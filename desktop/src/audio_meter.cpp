// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/audio_meter.h"

#include <algorithm>
#include <cmath>

using namespace edmx;

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
    // lasts until the next tick. Registered anyway because `params` is how a
    // desk reads a look's numbers, and "what is this channel doing right now"
    // is the single most useful number this look has.
    bag.addState("value", value);
    bag.addState("peak", peak);
}


// ============================================================================
// the cue list
// ============================================================================

namespace
{
    /// The family a channel belongs to, as a colour.
    ///
    /// So the rig says which family is up without anyone reading the pad, and
    /// - more usefully - so flipping through a family looks like one thing
    /// changing rather than eight unrelated cues.
    ecore::HSV colourFor(AudioChannel channel)
    {
        switch (channel)
        {
            // levels: white. The plain reading, and the one every other
            // family is a derivative of.
            case AudioChannel::Level:
            case AudioChannel::Bass:
            case AudioChannel::Mid:
            case AudioChannel::MidHigh:
            case AudioChannel::High:
                return ecore::HSV(0.0f, 0.0f, 1.0f);

            // hits: red. The transients, and the ones the show's own hit
            // layers are built on - same colour as the ring's layer, so a
            // measurement and the look it justifies read alike.
            case AudioChannel::Hits:
            case AudioChannel::BassHits:
            case AudioChannel::MidHits:
            case AudioChannel::MidHighHits:
            case AudioChannel::HighHits:
                return ecore::HSV(0.0f, 1.0f, 1.0f);

            // presence: blue.
            case AudioChannel::Presence:
            case AudioChannel::BassPresence:
            case AudioChannel::MidPresence:
            case AudioChannel::MidHighPresence:
            case AudioChannel::HighPresence:
                return ecore::HSV(210.0f, 1.0f, 1.0f);

            // the grid and the shape of the track: green.
            case AudioChannel::Beat:
            case AudioChannel::Bpm:
            case AudioChannel::BpmConfidence:
            case AudioChannel::Intensity:
                return ecore::HSV(120.0f, 1.0f, 1.0f);

            // Mixxx's own two meters: amber, because they are the ones that
            // stay dark on `audio.source: synesthesia` by design, and a cue
            // that is *meant* to be dead should not look like the accident.
            case AudioChannel::LevelInstant:
            case AudioChannel::LevelMeter:
                return ecore::HSV(38.0f, 1.0f, 1.0f);

            case AudioChannel::Count:
                break;
        }
        return ecore::HSV(0.0f, 0.0f, 1.0f);
    }

    StateDef channelLook(AudioChannel channel)
    {
        StateDef def;
        def.name = audioChannelName(channel);
        def.make = [channel]() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto look = std::make_shared<Pattern_Audio_Meter>();
            look->channel = channel;
            look->color = colourFor(channel);
            look->init();
            return look;
        };
        return def;
    }
}

std::unique_ptr<StateMachinePattern> edmx::makeAudioStateMachine()
{
    // Built from the enum, not from a list written here. A channel added to
    // AudioChannel becomes a cue on the rig and a row in the surface's table
    // with nothing in this file edited - which is the same reason
    // kAudioChannelNames is one table rather than one per caller.
    std::vector<StateDef> states;
    states.reserve(kAudioChannelCount);
    for (int index = 0; index < kAudioChannelCount; ++index)
    {
        states.push_back(channelLook(static_cast<AudioChannel>(index)));
    }

    // The show's own space. Nothing here is spatial, so this only has to be
    // something; it is mythos26's so that switching between the two does not
    // re-cut the stage underneath the layers.
    CoordFrame rig;
    rig.originX = 0.0f;
    rig.spanX   = 0.0f;
    rig.originY = 0.0f;
    rig.spanY   = 1.0f;

    // No cross-fade. Every other cue list here blends because a show should
    // not cut; this one is an instrument, and a fade between two channels is a
    // quarter second of a number that is neither of them.
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "audio", std::move(states), /*segmentId*/ 0, rig, 0.0f));
}
