// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lib/eanim/generator_hsv.h"
#include "lib/eio/relic.h"
#include "lib/eio/strip_projection.h"
#include "lib/esm/state.h"
#include "lib/esm/state_generic.h"

#include "edmx/pattern.h"

namespace edmx
{
    /// A RelicIO with nothing behind it.
    ///
    /// The relic states render through whatever strip segments their IO holds,
    /// so to run them here we only have to hold some. There is no screen and
    /// there are no buttons: getScreenDrawer() stays null, which every state
    /// already guards for, and input arrives over the control protocol instead.
    class HostRelicIO : public eio::RelicIO
    {
    public:
        /// Builds one segment of `count` nodes, tagged with `segmentId`.
        ///
        /// The id matters more than it looks. Relic patterns branch on which
        /// segment a node belongs to — the jacket's looks render differently on
        /// a sleeve ring than on the monowire — so the id we claim here decides
        /// which branch a rig gets. See JACKET_SEGMENT_ID.
        void build(size_t count, uint8_t segmentId, const CoordFrame& frame,
                   const std::vector<float>& positions);

        eio::HSVStrip* getStrip() const { return strip.get(); }

    private:
        std::unique_ptr<eio::HSVStrip> strip;
        std::vector<std::shared_ptr<eio::HSVStripNode_Mapped2D>> nodes;
    };


    /// One entry in a state machine's table of looks.
    ///
    /// `make` builds the generator. `applyInput` is optional and forwards the
    /// two momentary inputs the relic patterns expose; leave it empty for a
    /// look that does not take input.
    struct StateDef
    {
        std::string name;
        std::function<std::shared_ptr<eanim::GeneratorHSV>()> make;
        std::function<void(eanim::GeneratorHSV*, bool inputA, bool inputB)> applyInput;

        /// Optional, and only for looks that fire on the beat: how many beats
        /// go by between hits. Leave it empty for a look that does not pulse.
        ///
        /// A function rather than a virtual on the generator for the same
        /// reason applyInput is one — the library builds without RTTI on the
        /// microcontroller and a dynamic_cast here would diverge the two. The
        /// table knows the concrete type; nothing else needs to.
        std::function<void(eanim::GeneratorHSV*, int beatsPerPulse)> applyDivision;

        /// What this look opens on, when the machine has no override set.
        int defaultBeatsPerPulse{1};
    };


    /// Runs a relic state machine as a DMX pattern.
    ///
    /// Where GeneratorPattern runs a single look, this runs a whole esm state
    /// machine: many looks, with the real cross-fade between them that the
    /// relics use. Switching state is a protocol command rather than a physical
    /// button, which is what lets a UI drive it.
    class StateMachinePattern : public Pattern
    {
    public:
        StateMachinePattern(const char* inName, std::vector<StateDef> inStates,
                            uint8_t inSegmentId, const CoordFrame& inFrame,
                            float inTransitionTime);

        const char* getName() const override { return name.c_str(); }

        void tick(float deltaTime) override;
        void render(const PatternContext& context, std::vector<ecore::HSV>& outColors) override;

        CoordFrame defaultCoordFrame() const override { return frame; }
        StateMachinePattern* asStateMachine() override { return this; }

        /// Every state, in table order. This is what a UI builds buttons from.
        std::vector<std::string> stateNames() const;

        /// The state being shown, or transitioned to.
        std::string currentStateName() const;

        /// Cross-fades to `stateName`. False and outError if there is no such
        /// state; asking for the state already showing is a no-op, not an error.
        bool setState(const std::string& stateName, std::string& outError);

        /// The two momentary inputs the relic patterns read. On a jacket these
        /// are remote buttons; here they are whatever the UI wires them to.
        void setInput(bool inputA, bool inputB);

        /// How many beats between hits, for every look here that pulses:
        /// 1 on every beat, 2 on every other, 4 once a bar.
        ///
        /// Zero means "each look's own default", which is how a machine holding
        /// both an on-every-beat look and an on-twos look can switch between
        /// them without the operator having to reset this each time. Any other
        /// value overrides all of them, because a deliberate choice at the desk
        /// should survive a cue change.
        void setBeatDivision(int beatsPerPulse);
        int getBeatDivision() const { return beatDivision; }

        /// What the running look is actually pulsing on, override or default.
        int currentBeatDivision() const;

    private:
        void ensureBuilt(const PatternContext& context);

        std::string name;
        std::vector<StateDef> states;
        uint8_t segmentId;
        CoordFrame frame;
        float transitionTime;

        bool inputA{false};
        bool inputB{false};

        /// 0 = each look's own default; see setBeatDivision.
        int beatDivision{0};

        /// Carried from tick() to render(): the machine cannot be ticked until
        /// the rig's shape is known, and only render() is handed that.
        float pendingDelta{0.0f};

        std::unique_ptr<HostRelicIO> io;
        // StateMachine_GenericHSV and State_GenericHSV sit in the global
        // namespace, unlike the rest of esm.
        std::unique_ptr<StateMachine_GenericHSV> machine;
        std::unique_ptr<esm::StateManager> manager;

        /// index-aligned with `states`
        std::vector<std::shared_ptr<State_GenericHSV>> instances;
        std::vector<std::shared_ptr<eanim::GeneratorHSV>> generators;

        size_t builtFor{0};
        size_t activeIndex{0};
    };


    /// The jacket's looks, as a state machine.
    ///
    /// Hardcoded on purpose — see the table in state_machine.cpp, where adding
    /// a look is one line.
    std::unique_ptr<StateMachinePattern> makeJacketStateMachine();
}
