// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "button.h"

#include "../ecore/logging.h"

using namespace eio;

void Button::init(uint8_t inPin)
{
    pinMode(inPin, INPUT_PULLUP);
    myPin = inPin;
}

bool Button::pollPressed()
{
    return !digitalRead(myPin);
}

void Button::tick(float deltaTime)
{
    timeSinceStateChanged += deltaTime;
    if(pollPressed())
    {
        if(!bLastSeenPressed)
        {
            bLastSeenPressed = true;
            timeSinceStateChanged = 0.0f;
        }
    }
    else
    {
        if(bLastSeenPressed)
        {
            bLastSeenPressed = false;
            timeSinceStateChanged = 0.0f;
        }
    }
}

void Button::resetTimeSinceStateChange()
{
    timeSinceStateChanged = 0.0f;
}