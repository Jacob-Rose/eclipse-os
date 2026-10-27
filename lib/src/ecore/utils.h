// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "range.h"

namespace ecore
{
    /* @brief remap an input to a new basis
    *   TODO ALL
    */
    struct FloatRemapper
    {
        FloatRange inRange;
        FloatRange outRange;

        float evaluate(float value) const {
            float inDiff = inRange.max - inRange.min;
            float alpha = (value - inRange.min) / inDiff;
            float outDiff = outRange.max - outRange.min;
            return (alpha * outDiff) + outRange.min;
        }
    };
}