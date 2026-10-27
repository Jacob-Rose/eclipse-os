// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <chrono>

#include "tickable.h"
#include "delegate.h"


namespace ecore
{
    class Timer : public Tickable
    {
    public:
        void startTimer(float timerTime);

        virtual void tick(float deltaTime);

        MulticastDelegate<> onTimerEvent; // event to call when the timer goes off

        std::chrono::time_point<std::chrono::high_resolution_clock> timerEndTime; // start time of the timer
    };
}