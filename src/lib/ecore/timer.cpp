// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "timer.h"

using namespace ecore;

void ecore::Timer::startTimer(float timerTime)
{
    timerEndTime = std::chrono::high_resolution_clock::now();
    timerEndTime += std::chrono::seconds(static_cast<int>(timerTime));
}

void Timer::tick(float deltaTime)
{
    if(timerEndTime == std::chrono::time_point<std::chrono::high_resolution_clock>()) // no timer set
    {
        return; // nothing to do
    }

    chrono::duration<float> elapsed = std::chrono::high_resolution_clock::now() - timerEndTime;
    if (elapsed.count() >= 0) // timer has gone off
    {
        timerEndTime = std::chrono::time_point<std::chrono::high_resolution_clock>();
        onTimerEvent.invoke(); // call the event
    }
}
