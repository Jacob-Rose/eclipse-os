// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>

#include "lib/eanim/generator_hsv.h"
#include "lib/ecore/hsv.h"

#include "edmx/beat_clock.h"
#include "edmx/state_machine.h"

///
/// The audio bus, on the rig: one cue per channel, for reading it with your
/// eyes instead of a terminal.
///
/// Every other look here is a show. This one is an instrument. It exists
/// because the analysis wire has four places to go wrong that all present
/// identically as "the lights are not moving" - the app's output is off, its
/// address is wrong, a firewall is eating it, or the map's globs do not match
/// this build - and because a fifth, which is much worse, presents as nothing
/// at all: a uniform the app publishes and never fills.
///
/// That last one is not hypothetical. `/audio/hits/high` on the build this was
/// written against sits at exactly zero forever while every band around it
/// moves, so the look bound to it is dark and correct and completely dead, and
/// no amount of staring at the rig says which. `channels` at the desk says it,
/// and so does this - from across the room, without a keyboard, which is where
/// the question is actually asked.
///
/// Deliberately not spatial. A meter that filled up the rig would read as a
/// different number on each device: the obelisk keeps its own 0..43 rather
/// than being squashed into the show's 0..1 (that is what makes its own looks
/// work), the ring stands in literal stage coordinates, and the truss is
/// offset past both. So this is whole-rig brightness, which means the same
/// thing everywhere and cannot be misread by standing somewhere else.
///

namespace edmx
{
    /// One channel of the bus, as light.
    ///
    /// Brightness is the value. Colour is the family, so which cue is up is
    /// readable without looking at the pad. And a channel nobody is filling
    /// is *not* drawn as zero - see `live`, which is the whole point.
    class Pattern_Audio_Meter : public eanim::GeneratorHSV
    {
    public:
        /// Which channel this cue reads. Set per state; not a knob, because a
        /// cue that could be pointed anywhere would make the pad lie.
        AudioChannel channel{AudioChannel::Level};

        /// The family's colour. Set per state alongside `channel`.
        ecore::HSV color{0.0f, 0.0f, 1.0f};

        /// What a full-scale reading maps to, and the floor under it. Knobs
        /// because a rig in a bright room and a rig in a dark one do not read
        /// the same, and this is meant to be read.
        float gain{1.0f};
        float floorLevel{0.0f};

        /// Seconds for the peak marker to fall back to the live value.
        ///
        /// Without it the hits channels are close to unreadable: a transient
        /// is two or three frames wide, and at thirty frames a second the eye
        /// gets a flicker it cannot size. The peak holds the top of the last
        /// spike and sags out of it, so a hit leaves something to look at.
        /// Zero switches it off and shows the raw value.
        float peakDecay{0.9f};

        /// How much of the peak shows under the live value. Not the whole of
        /// it: at 1 the meter would sit at its high-water mark and never come
        /// down, which reads as a channel that is pinned rather than one that
        /// is spiking.
        float peakMix{0.45f};

        void init();

        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* node, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// The last reading and whether the channel is being fed, for tests.
        float getValue() const { return value; }
        bool isLive() const { return live; }

    private:
        AudioLevel* meter{nullptr};
        float value{0.0f};
        float peak{0.0f};

        /// Whether the channel has been fed recently enough to trust. Read
        /// every frame rather than derived from `value`, because zero and
        /// "nobody is sending" are the two states this look exists to tell
        /// apart and they are identical in the number alone.
        bool live{false};

        /// Seconds since this look came up, for the dead-channel blink.
        float clock{0.0f};
    };


    /// One cue per channel of the audio bus, in the bus's own order.
    ///
    /// Built from the channel table rather than from a list written here, so a
    /// channel added to AudioChannel is a cue on the rig and a pad on the
    /// surface with nothing else edited. Two lists that must agree is one list
    /// that eventually does not - see kAudioChannelNames.
    std::unique_ptr<StateMachinePattern> makeAudioStateMachine();
}
