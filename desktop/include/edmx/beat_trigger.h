// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>
#include <vector>

///
/// When a hit lands — decided once, for the whole rig.
///
/// The clock says where the beat is. This says which of those beats are *hits*,
/// and it is a separate file because that is a separate question with a
/// separate failure mode.
///
/// **Why it is not left to the looks.** Every beat look used to work this out
/// for itself: read the clock, watch the beat number change, check the rate,
/// fire its envelope. Four looks meant four copies of that, each with its own
/// idea of which beat it was on, and they drifted apart in three ways that all
/// read on a rig as "the UV is not with the show":
///
///   - **A look that is not showing is not counting.** Only the active state of
///     a machine ticks (and the incoming one, mid-fade), so a look entered cold
///     had stale bookkeeping and had to guess.
///   - **Entry fired a hit.** Coming up dark reads as a cue that did not come
///     up, so the first tick always hit — at whatever fraction of a beat the
///     operator happened to press the button, which is off the grid by
///     construction.
///   - **A cross-fade ticks both looks**, so the outgoing and incoming
///     envelopes fired independently for the length of the fade.
///
/// So the decision moves here, is made **once per frame before anything ticks**,
/// and is shared. Two looks on the same rate now hit on the same frame with the
/// same sub-frame offset, because they are reading the same answer rather than
/// each computing one.
///
/// **What a look still owns** is the shape. A trigger says *when*; the curve on
/// the look says what the light does about it. That split is the whole point:
/// the UV cracks in 20ms and the truss swells over 150ms, on the same beat.
///
/// **Rates are the binding.** There is no string knob on a property bag, and
/// there does not need to be one: the four musical rates are the four triggers,
/// and a look's existing `rate` knob is which of them it is bound to. Anything
/// at half time is on the `half` trigger, so half time is one decision for the
/// rig instead of one per look that happened to agree.
///
namespace edmx
{
    class BeatClock;

    /// The four rates, in hits per beat.
    ///
    /// Named rather than left as loose numbers because 1.37 hits a beat is a
    /// rig drifting against the track, and a slider will otherwise hand one
    /// over on the way past.
    constexpr float kQuarterTime{0.25f};
    constexpr float kHalfTime{0.5f};
    constexpr float kOnBeat{1.0f};
    constexpr float kDoubleTime{2.0f};

    /// Snaps to the nearest of the four. Anything between them is not a rate.
    float snapPulseRate(float pulsesPerBeat);

    /// The trigger a rate binds to: `bar`, `half`, `beat`, `double`.
    const char* triggerNameForRate(float pulsesPerBeat);

    /// One trigger's answer for this frame.
    ///
    /// Read-only to a look: the rack fills it in, the look asks. `fired` is
    /// true for exactly the frame the hit lands in.
    struct BeatTrigger
    {
        std::string name;
        float rate{kOnBeat};

        /// A hit landed in this frame.
        bool fired{false};

        /// How long ago inside the frame, in seconds.
        ///
        /// Not zero, and this is the part worth keeping. A beat lands anywhere
        /// inside a frame; at 40fps that is a 25ms window. Firing the envelope
        /// at the frame boundary quantises every hit to the frame grid and puts
        /// a visible swing on the rig, so the impulse is fired at where it
        /// actually happened.
        float sinceHit{0.0f};

        /// Hits since the process started, for status and for tests.
        unsigned long long hits{0};

        /// The beat this trigger last saw. Its own, not the clock's, so a rate
        /// that skips beats still knows which one it acted on.
        long long lastBeat{0};

        /// Double time's mid-beat hit, once per beat. Cleared by the beat.
        bool offbeatFired{false};

        /// False until the first frame, so the rack can seat itself on the grid
        /// without firing. A look that *wants* a hit on entry asks for one; see
        /// TriggerRack::armEntry.
        bool started{false};
    };

    /// Every trigger the rig has, evaluated together.
    ///
    /// A rack rather than four globals because the set is walked once a frame
    /// and is announced to the desk as a set.
    class TriggerRack
    {
    public:
        TriggerRack();

        /// Works out what every trigger does this frame. Called once, from the
        /// frame loop, **before any pattern ticks** — that ordering is the
        /// guarantee the whole file exists to provide.
        void tick(double now);

        /// The trigger for a rate, snapped. Never null: the four are built in
        /// the constructor and the rack is fixed after that.
        const BeatTrigger& forRate(float pulsesPerBeat) const;

        /// By name, or null if there is no such trigger.
        const BeatTrigger* find(const std::string& name) const;

        /// Every trigger, in rate order — slowest first, the way a desk lists
        /// them.
        const std::vector<BeatTrigger>& all() const { return triggers; }

        /// Fire `name` on the next tick regardless of the grid.
        ///
        /// This is what a cue coming up lit is made of. A look entering wants a
        /// hit now rather than in up to a bar's time, and it must not get one
        /// by keeping bookkeeping of its own — that is exactly the drift this
        /// file removes. So it asks the rack, the rack fires the shared
        /// trigger, and *everything* on that trigger hits together. A cue
        /// change is a rig-wide event; this makes it look like one.
        ///
        /// Cleared by the tick that acts on it. Asking twice before a frame is
        /// the same as asking once.
        void armEntry(const std::string& name);

        /// `beat=1418 bar=2 fired=beat,double`, for STATUS.
        std::string describe(double now) const;

    private:
        /// Fills in one trigger against the clock.
        void evaluate(BeatTrigger& trigger, double now);

        /// Beats from one hit to the next at this rate: 4 once a bar, 2 at half
        /// time, 1 at anything the beat itself keeps up with.
        static int beatsPerHit(float rate);

        BeatClock* clock{nullptr};
        std::vector<BeatTrigger> triggers;
        std::vector<std::string> armed;
    };

    /// The process's triggers.
    ///
    /// A singleton for the same reason the clock is one: patterns are built by
    /// factories that take no arguments and are handed no context. There is one
    /// beat in a room, so there is one set of hits off it.
    TriggerRack& sharedTriggerRack();
}
