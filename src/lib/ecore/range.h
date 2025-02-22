// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <numeric>
#include <random>

using namespace std;

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

        float getRandomValueInRange() const {
            static thread_local std::mt19937 generator(std::random_device{}());
            std::uniform_real_distribution<float> distribution(min, max);
            return distribution(generator);
        }
    };

    using FloatRange = Range<float>;
    using IntRange = Range<int>;
}