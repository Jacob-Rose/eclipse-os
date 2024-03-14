// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "io.h"
#include <memory>
#include <vector>

template<typename T>
class Keyframe
{
    T Value;
    float Loc;
}

// type t needs to support 
template<typename T>
struct Attribute
{
    std::vector<Keyframe<T>> Keyframes;
}