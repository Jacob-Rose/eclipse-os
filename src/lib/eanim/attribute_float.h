// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once


#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

namespace eanim
{
    /* @brief FloatAttribute is an interface to allow for a simple getter value
    * this gives us a uniform interface to build up a set of FloatAttributes and Generator1Ds that can be fed into GeneratorHSVs
    *
    * @see FloatProcessor
    * @see Sweep
    * @see Spring
    */
    class FloatAttribute
    {
    public:
        virtual float getValue() const = 0;
    };
}