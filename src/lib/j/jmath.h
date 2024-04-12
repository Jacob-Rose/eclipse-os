// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"

#include <algorithm>
#include <cmath>
#include <vector>

// https://gist.github.com/laundmo/b224b1f4c8ef6ca5fe47e132c8deab56
// converted to c++

        
// Linear interpolate on the scale given by a to b, using t as the point on that scale.
//      Examples
//      --------
//      50 == lerp(0, 100, 0.5)
//      4.2 == lerp(1, 5, 0.8)
float lerp(const float& a, const float& b, const float& t);

    
        
// https://gist.github.com/laundmo/b224b1f4c8ef6ca5fe47e132c8deab56
// converted to c++

/// Inverse Linar Interpolation, get the fraction between a and b on which v resides.
///     Examples
///     --------
///     0.5 == inv_lerp(0, 100, 50)
///     0.8 == inv_lerp(1, 5, 4.2)

float inv_lerp(const float& a, const float& b, const float& t);

// https://gist.github.com/laundmo/b224b1f4c8ef6ca5fe47e132c8deab56
/// Remap values from one linear scale to another, a combination of lerp and inv_lerp.
/// i_min and i_max are the scale on which the original value resides,
/// o_min and o_max are the scale to which it should be mapped.
///     Examples
///     --------
///     45 == remap(0, 100, 40, 50, 50)
///     6.2 == remap(1, 5, 3, 7, 4.2)

float remap(const float& i_min, const float& i_max, const float& o_min, const float& o_max, const float& v);

//
// Written by Jake Rose past this

float lerp_keyframes(float a, const std::vector<float>& keys);

float get_index_from_alpha(float a, const std::vector<float>& keys);

float get_random_float();

float get_random_float_in_range(float min, float max);