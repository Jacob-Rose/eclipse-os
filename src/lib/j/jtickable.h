// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"

#include <Arduino.h>

#include <string>
#include <vector>
#include <memory>


namespace j
{
    class Tickable
    {
        virtual void tick(float deltaTime) = 0;
    };
}