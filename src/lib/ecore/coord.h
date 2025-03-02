// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <vector>
#include <memory>

using namespace std;

namespace ecore
{
    struct Coordinate
    {
    public:
        Coordinate() : x(0.0f), y(0.0f) {}
        Coordinate(float x, float y);
        float x;
        float y;
    };

    typedef Coordinate Coord;
}