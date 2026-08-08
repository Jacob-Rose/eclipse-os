// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>

#ifndef USE_ARDUINO
#define USE_ARDUINO 1
#endif

#if USE_ARDUINO
#include "pico/stdlib.h"
#else
#include <cstdint>
#endif

///
/// Fixed point standards for getting floating point style ranges with much more performant fixed point arithmetic.
///


namespace efp
{
    // fixed-point int
    typedef uint16_t fpInt;
    // this feels like a good balance of scale while avoiding rolling over size of fpInt. 
    // probably needs to be updated to uint32_t and raised if we go up in size.
    static constexpr fpInt SCALE_FACTOR = 12000;//(std::numeric_limits<fpInt>::max() / 2)-1;

    // returns float between 0.f - 1.f
    // value / SCALE_FACTOR
    // 0 < fpInt < SCALE_FACTOR
    //
    // defined here rather than in fp.cpp: as a `static` declaration in a header
    // every translation unit got its own internal-linkage copy with no
    // definition behind it, so calling this would not have linked.
    inline float getFloat(fpInt value)
    {
        return static_cast<float>(value) / SCALE_FACTOR;
    }
}