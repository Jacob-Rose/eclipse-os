// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../lib/ecore/core.h"
#include "../lib/esm/state.h"
#include "../lib/esm/state_generic.h"

#include "../lib/eio/relic.h"

using namespace ecore;
using namespace eio;
using namespace esm;

namespace obelisk
{    
    enum class ObeliskStripID
    {
        STRIP_MAIN
    };
    
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

        uint16_t StripLEDPin = 22; // GPIO 6
    };
    
    class ObeliskCore : public RelicCore
    {
    public:
        ObeliskCore();

        virtual void tick(float deltaTime) override;
        virtual void handleCommand(string msg) override;

    protected:
        std::unique_ptr<StateMachine_GenericHSV> stateMachine{ nullptr };
        std::unique_ptr<StateManager> stateManager{ nullptr };

        
        shared_ptr<State_GenericHSV> mainPatternState;
        shared_ptr<State_GenericHSV> theaterPatternState;
        shared_ptr<State_GenericHSV> testPatternState;
        int mainPatternId{0};
        int theaterPatternId{0};
        int testPatternId{0};
    };
}