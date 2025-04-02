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

    constexpr int RING_ONE_LENGTH = 35;
    constexpr int RING_TWO_LENGTH = 28;
    constexpr int RING_THREE_LENGTH = 24;
    constexpr int RING_FOUR_LENGTH = 24;
    constexpr int RING_FIVE_LENGTH = 21;
    constexpr int RING_SIX_LENGTH = 20;
    constexpr int RING_SEVEN_LENGTH = 18;
    constexpr int MONOWIRE_LENGTH = 52;

    constexpr bool RING_ONE_FORWARD = true;
    constexpr bool RING_TWO_FORWARD = true;
    constexpr bool RING_THREE_FORWARD = true;
    constexpr bool RING_FOUR_FORWARD = true;
    constexpr bool RING_FIVE_FORWARD = false;
    constexpr bool RING_SIX_FORWARD = false;
    constexpr bool RING_SEVEN_FORWARD = false;

    class JacketIO : public PendantIO
    {
    public:
        JacketIO();

        virtual void init() override;

        virtual void tick(float deltaTime) override;
    };
}