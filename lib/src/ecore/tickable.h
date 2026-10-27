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
}