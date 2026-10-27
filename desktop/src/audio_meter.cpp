// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edesk/audio_meter.h"

#include <algorithm>
#include <cmath>

using namespace edesk;
using namespace ebeat;

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
            case AudioChannel::Sub:
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

            // Mixxx's own three meters: amber, because they are the cable's
            // whichever source owns the rest of the bus - lit under the
            // visualiser only if Mixxx is sending, and dark with no accident
            // behind it when the mapping's box for that note is unticked.
            case AudioChannel::LevelInstant:
            case AudioChannel::LevelAverage:
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

std::unique_ptr<StateMachinePattern> edesk::makeAudioStateMachine()
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

    // And the beat flash on the end, which is not a channel and is not
    // generated: the show that owned it is empty for now, and of the lists
    // here this is the one a beat-driven look belongs in. See beatPulseState.
    states.push_back(beatPulseState());

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
