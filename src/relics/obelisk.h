// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../lib/esm/state.h"

#include "relic.h"

using namespace ecore;
using namespace eio;

using namespace std;

namespace obelisk
{    
    enum class ObeliskStripID : uint8_t
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
    public:
    };
    
    class ObeliskCore : public RelicCore
    {
    public:
        ObeliskCore();
    };
}