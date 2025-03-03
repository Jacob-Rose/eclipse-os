// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../lib/ecore/core.h"
#include "../lib/esm/state.h"

#include "relic.h"

using namespace ecore;
using namespace eio;

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
    
        virtual void init();

        uint16_t StripLEDPin = 6; // GPIO 6
    };
    
    class ObeliskCore : public RelicCore
    {
    public:
        ObeliskCore();

        virtual void tick(float deltaTime) override;
    };
}