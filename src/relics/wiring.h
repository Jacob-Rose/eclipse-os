// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../lib/eio/hsv_strip.h"

// ---------------------------------------------------------------------------
// Where each sculpture's pixels are plugged in.
//
// This is the whole list, and the only place a pin, a length or a colour
// order is written down. It exists because the wiring is the one thing a
// branch cannot tell you: the string lights were GP5 x 300 BGR on `dev` and
// GP13 x 60 GRB here, both compiled, and the wrong one is a dark strip with
// nothing in the log.
//
// A relic takes its strip from here by name, and tools/firmware-env.sh greps
// the same line to print it before every flash and in the boot banner - so
// keep each entry on one line, in this shape:
//
//     constexpr eio::StripWiring kName { pin, length, order };
// ---------------------------------------------------------------------------
namespace wiring
{
    constexpr eio::StripWiring kObelisk    { 6, 344, NEO_GRB + NEO_KHZ800 }; // 344 = WALL_SIDE_LENGTH * 8, asserted in obelisk.cpp
    constexpr eio::StripWiring kWhiteboard { 5, 300, NEO_BGR + NEO_KHZ800 }; // the string lights on the wall
}
