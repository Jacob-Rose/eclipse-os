// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../lib/esm/state.h"
#include "../lib/esm/state_generic.h"
#include "../lib/eio/relic.h"
#include "pendant.h"

using namespace ecore;
using namespace eio;

using namespace pendant;

namespace jacket
{
    enum class JacketSegmentID
    {
        RING_ONE,
        RING_TWO,
        RING_THREE,
        RING_FOUR,
        RING_FIVE,
        RING_SIX,
        RING_SEVEN,
        MONOWIRE,
        MAX
    };

    constexpr int RING_ONE_LENGTH = 42;
    constexpr int RING_TWO_LENGTH = 42;
    constexpr int RING_THREE_LENGTH = 42;
    constexpr int RING_FOUR_LENGTH = 42;
    constexpr int RING_FIVE_LENGTH = 42;
    constexpr int RING_SIX_LENGTH = 42;
    constexpr int RING_SEVEN_LENGTH = 22;
    constexpr int MONOWIRE_LENGTH = 42;

    constexpr bool RING_ONE_FORWARD = true;
    constexpr bool RING_TWO_FORWARD = true;
    constexpr bool RING_THREE_FORWARD = true;
    constexpr bool RING_FOUR_FORWARD = true;
    constexpr bool RING_FIVE_FORWARD = true;
    constexpr bool RING_SIX_FORWARD = true;
    constexpr bool RING_SEVEN_FORWARD = true;

    class JacketIO : public RelicIO
    {
    public:
        JacketIO();

        virtual void init() override;

        void tick2();

    protected:
        std::unique_ptr<PendantIO> pendant;
    };

    class JacketCore : public RelicCore
    {
    public:
        JacketCore();

        virtual void init() override;

        void tick2();
        
    protected:
        std::unique_ptr<StateMachine_GenericHSV> stateMachine{ nullptr };
        std::unique_ptr<StateManager> stateManager{ nullptr };
        
        std::shared_ptr<State_GenericHSV> mainPatternState;
    };
}

