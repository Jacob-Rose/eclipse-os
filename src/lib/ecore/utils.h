// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

namespace ecore
{
    /* @brief remap an input to a new basis
    *   TODO ALL
    */
    template<typename T>
    struct Range
    {
        Range() {}
        Range(T inMin, T inMax) : min(inMin), max(inMax) {}

        T min;
        T max;

        bool isWithinRange(T val) const {
            return min < val && val < max;
        }
    };

    using FloatRange = Range<float>;
    using IntRange = Range<int>;

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