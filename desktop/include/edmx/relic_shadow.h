// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "lib/eanim/generator_hsv.h"
#include "lib/ecore/hsv.h"
#include "lib/eio/relic.h"
#include "lib/eio/strip_projection.h"

class State_GenericHSV;
class StateMachine_GenericHSV;

namespace esm { class StateManager; }

namespace edmx
{
    /// What a shadow needs to know about one kind of relic to run its looks
    /// here: the sculpture's own IO layer for its geometry, its looks by the
    /// names its firmware answers `sim` with, and which object on the stage
    /// its nodes are. Nothing about the relic itself lives in RelicShadow;
    /// a new sculpture is a new profile, registered beside the obelisk's in
    /// relic_shadow_profiles.cpp.
    struct RelicShadowProfile
    {
        /// The identity the relic answers a Hello with (`EOSLINK hello obelisk`).
        std::string relic;

        /// Which object its nodes belong to, for the underlay to answer only
        /// for them.
        eio::NodeSpace space{eio::NodeSpace::Stage};

        /// The relic's own IO, built for the host: its strips with their
        /// nodes and coordinates, exactly as the firmware lays them out.
        std::function<std::unique_ptr<eio::RelicIO>()> makeIO;

        /// One of its looks by name, or null for a name it does not have.
        std::function<std::shared_ptr<eanim::GeneratorHSV>(const std::string&)> makeLook;
    };

    /// The profile for a relic by its identity, or null when no sculpture of
    /// that name has one here - which the show reports rather than guesses
    /// around.
    const RelicShadowProfile* findShadowProfile(const std::string& relic);


    /// A relic's own look, running here in step with the sculpture.
    ///
    /// When the desk takes a relic's pixels the relic stops drawing, and what
    /// it *would* be drawing is exactly what a look that wants to join the
    /// sculpture rather than paint over it needs to know. Streaming the
    /// picture back up the cable would cost more than the picture down it.
    /// So instead the relic answers one line - which look, and where its
    /// clocks are (`EOSLINK sim seasons noise.time=812.4`) - and this spawns
    /// the same look class on the same geometry, puts its clocks there, and
    /// ticks it beside the show. The code is the relic's own, compiled for
    /// the host: same noise, same palette, same pixel.
    ///
    /// It is an eanim::Underlay, so a look samples it by node. Only nodes on
    /// the profile's object answer.
    ///
    /// The relic keeps its clocks moving under the takeover (see
    /// RelicCore::tickWhileLinked and what the obelisk does with it), so a
    /// copy put in step once stays in step; the desk re-asks every few
    /// seconds anyway, because two clocks are two clocks.
    class RelicShadow : public eanim::Underlay
    {
    public:
        RelicShadow();
        ~RelicShadow() override;

        /// Which relic this shadows. Clears any running look when it
        /// changes. Null means no shadow can be built, and applySim says so.
        void setProfile(const RelicShadowProfile* inProfile);
        const RelicShadowProfile* getProfile() const { return profile; }

        /// The text after `EOSLINK sim `: a look name and its state. Spawns
        /// the look when it is not the one running, then applies the state.
        /// False, with a reason, when there is no profile or it has no such
        /// look - the shadow is then cleared, because a stale look is worse
        /// than none.
        bool applySim(const std::string& text, std::string& outError);

        /// Advances the look. Nothing before the first applySim.
        void tick(float deltaTime);

        void clear();

        bool isLive() const { return machine != nullptr; }
        const std::string& getLookName() const { return lookName; }

        /// Seconds since the last applySim; large means the relic has gone
        /// quiet on `sim` and the copy is running on its own clock.
        double sinceSync() const;

        /// The look's state here, `name=value` - what a `sim` would answer
        /// if the copy were asked.
        std::string describeState() const;

        /// The colour at one pixel of the relic's first strip, in strip order.
        ecore::HSV colorAt(uint16_t stripIdx) const;
        uint16_t pixelCount() const;

        // -- eanim::Underlay --
        bool sample(const eio::HSVStripNode* node, ecore::HSV& outColor) const override;

    private:
        bool spawn(const std::string& name, std::string& outError);
        const eio::HSVStrip* firstStrip() const;

        const RelicShadowProfile* profile{nullptr};

        std::unique_ptr<eio::RelicIO> io;
        std::unique_ptr<esm::StateManager> manager;
        std::unique_ptr<StateMachine_GenericHSV> machine;
        std::shared_ptr<State_GenericHSV> state;
        std::shared_ptr<eanim::GeneratorHSV> look;
        std::string lookName;
        std::chrono::steady_clock::time_point syncedAt;
    };
}
