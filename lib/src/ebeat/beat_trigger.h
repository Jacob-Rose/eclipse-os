// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>
#include <vector>

namespace ebeat
{
    class BeatClock;

    /// The four musical rates, in hits per beat. A look's `rate` knob snaps
    /// to one of them; anything between is a rig drifting against the track.
    constexpr float kQuarterTime{0.25f};
    constexpr float kHalfTime{0.5f};
    constexpr float kOnBeat{1.0f};
    constexpr float kDoubleTime{2.0f};

    /// The nearest of the four rates.
    float snapPulseRate(float pulsesPerBeat);

    /// The trigger a rate binds to: `bar`, `half`, `beat` or `double`.
    const char* triggerNameForRate(float pulsesPerBeat);

    /* @brief One trigger's answer for this frame. The rack fills it in; a look
    * reads it and shapes the light with its own curve.
    *
    *     const BeatTrigger& hit = rack.forRate(rate);
    *     if (hit.fired) envelope.trigger(hit.sinceHit);
    */
    struct BeatTrigger
    {
        std::string name;
        float rate{kOnBeat};

        /// A hit landed in this frame.
        bool fired{false};
        /// How far into the frame it landed, in seconds, so an envelope can
        /// start where the beat was rather than on the frame grid.
        float sinceHit{0.0f};
        /// Hits since the start, for status.
        unsigned long long hits{0};

        /// The beat this trigger last saw; its own, so a rate that skips beats
        /// knows which one it acted on.
        long long lastBeat{0};
        /// Double time's mid-beat hit, once per beat.
        bool offbeatFired{false};
        /// False until the first frame, which seats the trigger without firing.
        bool started{false};
    };

    /* @brief Which beats are hits, decided once a frame for the whole rig.
    *
    * Four triggers, one per rate, all read off one BeatClock. Tick it once a
    * frame before any look ticks, and every look on a rate hits on the same
    * frame with the same offset. A look entering that wants a hit now asks
    * for one with armEntry rather than keeping count of its own.
    *
    *     TriggerRack rack(clock);
    *     rack.tick(now);                        // first thing each frame
    *     rack.armEntry("beat");                 // a cue coming up lit
    *
    * @see BeatClock
    */
    class TriggerRack
    {
    public:
        explicit TriggerRack(BeatClock& inClock);

        /// Evaluates every trigger against the clock. Once a frame, before
        /// any look ticks.
        void tick(double now);

        /// The trigger for a rate, snapped. Never null.
        const BeatTrigger& forRate(float pulsesPerBeat) const;
        /// By name, or null.
        const BeatTrigger* find(const std::string& name) const;
        /// All four, slowest first.
        const std::vector<BeatTrigger>& all() const { return triggers; }

        /// Fire a trigger on the next tick regardless of the grid. Cleared by
        /// that tick; asking twice is asking once.
        void armEntry(const std::string& name);

        /// `beat=1418 bar=2 fired=beat,double`
        std::string describe(double now) const;

    private:
        void evaluate(BeatTrigger& trigger, double now);
        /// Beats from one hit to the next at a rate: 4 once a bar, 2 at half time, else 1.
        static int beatsPerHit(float rate);

        BeatClock* clock;
        std::vector<BeatTrigger> triggers;
        std::vector<std::string> armed;
    };
}
