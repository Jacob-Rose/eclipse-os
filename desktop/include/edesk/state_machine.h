// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <eanim/generator_hsv.h>
#include <eio/host_relic.h>
#include <eio/relic.h>
#include <eio/strip_projection.h>
#include <esm/state.h>
#include <esm/state_generic.h>

#include "edesk/pattern.h"

namespace edesk
{
    /// One entry in a state machine's table of looks.
    ///
    /// `make` builds the generator. `applyInput` is optional and forwards the
    /// two momentary inputs the relic patterns expose; leave it empty for a
    /// look that does not take input.
    ///
    /// `makeState` is optional and wraps the made generator in a custom
    /// State_GenericHSV subclass. The scanner's looks need this: their
    /// State_ScannerHSV rewinds the pattern's clock on entry, which is what
    /// makes a timed look like power_up start from its beginning each time
    /// rather than wherever its clock had drifted to.
    struct StateDef
    {
        std::string name;
        std::function<std::shared_ptr<eanim::GeneratorHSV>()> make;
        std::function<void(eanim::GeneratorHSV*, bool inputA, bool inputB)> applyInput;
        std::function<std::shared_ptr<State_GenericHSV>(
            const char* stateName, eio::RelicIO* io,
            std::shared_ptr<eanim::GeneratorHSV> generator)> makeState;

        /// Optional: how the look treats the sculpture when a layer is under
        /// it (live) or not. The scanner's looks join or leave the tower over
        /// the sculpture's own picture and paint it on their own; a look with
        /// no such choice leaves this empty.
        std::function<void(eanim::GeneratorHSV*, bool live)> composeOver;
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

        /// The knobs of the look that is *showing*, and nothing else.
        ///
        /// This is the whole point of gathering them per call rather than once:
        /// a cue change swaps which object the properties point at, so the set
        /// a UI is holding goes stale the moment the state does. Whoever asks
        /// gets the current look's, and emitParams() re-announces them on every
        /// state change.
        void reflect(ecore::PropertyBag& bag) override;

        /// To the look that is showing, same as the knobs.
        bool trigger(const ecore::GameplayTag& tag) override;

        /// Every state, in table order. This is what a UI builds buttons from.
        std::vector<std::string> stateNames() const;

        /// The state being shown, or transitioned to.
        std::string currentStateName() const;

        /// Cross-fades to `stateName`. False and outError if there is no such
        /// state; asking for the state already showing is a no-op, not an error.
        bool setState(const std::string& stateName, std::string& outError);

        /// How long the next cross-fades take, in seconds. This is how a
        /// caller that knows its own choreography - afterglow sends its
        /// transitionTo(state, time) pairs - gets each blend at its length.
        void setTransitionTime(float seconds);

        /// How the next cross-fades look - one of esm's built-in blends, by
        /// name (`cut`, `crossfade`, `rgb`, `dip`, `wipe`). Sticky like the
        /// time. False and outError, naming the choices, for a name that is
        /// not one of them.
        bool setBlend(const std::string& blendName, std::string& outError);
        std::string blendName() const;

        /// The two momentary inputs the relic patterns read. On a jacket these
        /// are remote buttons; here they are whatever the UI wires them to.
        void setInput(bool inputA, bool inputB);

        /// Rendered into the nodes ahead of the machine each frame, so every
        /// look - the two of a cross-fade included - starts from the layer's
        /// picture. Each state's composeOver is told whether one is there.
        void setUnderLayer(eanim::GeneratorHSV* under) override;

    private:
        void ensureBuilt(const PatternContext& context);

        eanim::GeneratorHSV* underLayer{nullptr};

        std::string name;
        std::vector<StateDef> states;
        uint8_t segmentId;
        CoordFrame frame;
        float transitionTime;
        const esm::StateBlend* blend{&esm::defaultStateBlend()};

        bool inputA{false};
        bool inputB{false};

        /// Carried from tick() to render(): the machine cannot be ticked until
        /// the rig's shape is known, and only render() is handed that.
        float pendingDelta{0.0f};

        std::unique_ptr<eio::HostRelicIO> io;
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

    /// The scanner's looks - afterglow's LED states - as a state machine.
    ///
    /// State names are the exact tags afterglow's python game transitions
    /// between, so the game can mirror itself here with `state <tag> [seconds]`
    /// and let this end render its pixels.
    std::unique_ptr<StateMachinePattern> makeScannerStateMachine();

    /// The generic looks - the free-standing stage patterns, most recreated
    /// from WLED - as one machine: each look a cue, with real cross-fades.
    std::unique_ptr<StateMachinePattern> makeGenericStateMachine();

    /// The obelisk's own ambient looks as one machine.
    std::unique_ptr<StateMachinePattern> makeObeliskStateMachine();

    /// One of the obelisk's own looks, by the name its firmware answers a
    /// `sim` with (seasons, theater, mono, blobs). Null for a name that is
    /// not one of them - a scanner tag, say - which a shadow reports rather
    /// than guesses at. The same classes ObeliskCore instantiates, so a copy
    /// spawned here and put at the same state draws the same pixels.
    std::shared_ptr<eanim::GeneratorHSV> makeObeliskLook(const std::string& name);

    /// The beat flash as a cue for someone else's list, with the shape it
    /// used to open on in the show.
    ///
    /// The show is twelve empty slots for now, and a look that fires on the
    /// beat still wants somewhere to be switched to; the audio bus is the
    /// nearest thing to a home - see makeAudioStateMachine. In mythos26.cpp,
    /// where the look and the beatLook that dresses it both live.
    StateDef beatPulseState();

    /// The UV par's own machine, for a layer: `off`, `flash` (a beat pulse
    /// with its envelope drawable at the desk) and `on`. Three modes for one
    /// light, run over the show rather than as part of it. In mythos26.cpp,
    /// beside the beat look it borrows.
    std::unique_ptr<StateMachinePattern> makeUvStateMachine();
}
