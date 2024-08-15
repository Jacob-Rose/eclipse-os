// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include "../ecore/tickable.h"

using namespace ecore;

namespace eio
{
    // buttons are self managing, making them able to track their state better
    // TODO add delegate system
    class Button : public Tickable
    {
    public:
        void init(uint8_t pin);

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // does not directly poll, must use tick to get updated value this frame
        bool isPressed() const { return bLastSeenPressed; }
        float getTimeSinceStateChange() const { return timeSinceStateChanged; }


        bool bHasBeenReleased = true; // hacky implementation done on necklace

        // lets us reset button timers easily
        void resetTimeSinceStateChange();
    private:
        bool pollPressed();

        uint8_t myPin;

        bool bLastSeenPressed = false;

        float timeSinceStateChanged = 0.0f;

        bool bInit = false;
    };

    // nice lil wrapper that can be passed in button refs and 
    // handle tap events, similar wrapper to Enhanced Input triggers

    // TODO
    class ButtonObserver
    {

    };
}