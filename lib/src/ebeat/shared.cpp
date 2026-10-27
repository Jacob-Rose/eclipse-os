// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "shared.h"

#include <ecore/core.h>

#if USE_ARDUINO
#include <Arduino.h>
#else
#include <chrono>
#endif

using namespace ebeat;

double ebeat::nowSeconds()
{
#if USE_ARDUINO
    return static_cast<double>(millis()) / 1000.0;
#else
    using clock = std::chrono::steady_clock;
    static const clock::time_point origin = clock::now();
    return std::chrono::duration<double>(clock::now() - origin).count();
#endif
}

BeatClock& ebeat::sharedBeatClock()
{
    static BeatClock instance;
    return instance;
}

TriggerRack& ebeat::sharedTriggerRack()
{
    static TriggerRack rack(sharedBeatClock());
    return rack;
}

AudioLevel& ebeat::sharedAudioLevel()
{
    static AudioLevel instance;
    return instance;
}
