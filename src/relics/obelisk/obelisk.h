// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../../lib/ecore/core.h"
#include "../../lib/ecore/timer.h"
#include "../../lib/esm/state.h"
#include "../../lib/esm/state_generic.h"

#include "../../lib/eio/relic.h"

#include "../scanner/scanner_patterns.h"

#include <map>
#include <string>

using namespace ecore;
using namespace eio;
using namespace esm;

namespace obelisk
{    
    enum class StripSegmentID
    {
        SideA_Up,
        SideA_Down,
        SideB_Up,
        SideB_Down,
        SideC_Up,
        SideC_Down,
        SideD_Up,
        SideD_Down,
        MAX
    };
    
    class ObeliskIO : public RelicIO
    {
    public:
        ObeliskIO();
    
        virtual void init() override;

        uint16_t StripLEDPin = 6;
    };
    
    class ObeliskCore : public RelicCore
    {
    public:
        ObeliskCore();

        virtual void tick(float deltaTime) override;
        virtual bool handleCommand(string msg) override;

        /// Under a desk's takeover the showing look keeps time without
        /// drawing: the desk is simulating it from a `sim` answer, and a
        /// handback should land on the picture the desk was showing, not on
        /// the frame the look was paused at. See State_GenericHSV::tickClocks.
        virtual void tickWhileLinked(float deltaTime) override;

        /// The look that is showing, by the name a `state` or `sim` line
        /// uses. Empty between states.
        std::string activeLookName() const;
        State_GenericHSV* activeLook() const;

    protected:
        /// A line back to whoever is on the other end of the link.
        void say(const string& line);

        /// Every state by its cue name - the ambient looks under their
        /// aliases, the scanner looks under afterglow's tags - and the
        /// reverse for naming the active one. What `state`, `states` and
        /// `sim` all read.
        std::map<std::string, shared_ptr<State_GenericHSV>> looksByName;

        std::unique_ptr<StateMachine_GenericHSV> stateMachine{ nullptr };
        std::unique_ptr<StateManager> stateManager{ nullptr };

        Timer stateChangeTimer; // timer for state transitions

        
        shared_ptr<State_GenericHSV> mainPatternState;
        shared_ptr<State_GenericHSV> theaterPatternState;
        shared_ptr<State_GenericHSV> testPatternState;
        /// the look the sculpture boots on - the seasons field generalised,
        /// two colours the desk can pick
        shared_ptr<State_GenericHSV> blobsPatternState;
        int mainPatternId{0};
        int theaterPatternId{0};
        int testPatternId{0};
        int blobsPatternId{0};

        // The scanner's looks, keyed by the exact state tags afterglow's
        // python game uses, so the scanner can mirror its state machine here
        // by sending `state <tag>` over the link as it transitions.
        std::map<std::string, shared_ptr<scanner::State_ScannerHSV>> scannerStates; // std:: - see relic.h
    };
}