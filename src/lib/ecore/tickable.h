// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

namespace ecore
{
    /* 
    * Tickable can be appended to any class that is made to perform actions on every tick. 
    * These functions should take as minimal time as possible to be performant.
    * 
    * DeltaTime is the amount of time that passed between the last tick and now. It is the same value for all tickable objects.
    */
    class Tickable
    {
    public:
        virtual void tick(float deltaTime) = 0;
    };


    class Timer : public Tickable
    {
    public:
        // returns true when the timer went off on the last tick
        // once it returns true it resets and will return false until timer goes off
        bool pollEvent();
        void startTimer(float timerTime);

        virtual void tick(float deltaTime);
    private:
        float timerStartTime = -1.f;
    };
}