// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <external/easing.h>

namespace
{
    // the ones rewritten away from the (--t) * t form, pinned at the ends and,
    // for the in-out ones, across the join at a half
    const easing_functions kRewritten[] = {
        easing_functions::EaseOutSine,  easing_functions::EaseOutCubic, easing_functions::EaseInOutCubic,
        easing_functions::EaseOutQuart, easing_functions::EaseInOutQuart,
        easing_functions::EaseOutQuint, easing_functions::EaseInOutQuint,
        easing_functions::EaseOutBack,  easing_functions::EaseInOutBack,
    };
    const easing_functions kInOut[] = {
        easing_functions::EaseInOutCubic, easing_functions::EaseInOutQuart,
        easing_functions::EaseInOutQuint, easing_functions::EaseInOutBack,
    };
}

TEST(easing_curves_start_at_zero_and_end_at_one)
{
    for (easing_functions which : kRewritten)
    {
        const easingFunction fn = getEasingFunction(which);
        CHECK_NEAR(fn(0.0), 0.0, 1e-9);
        CHECK_NEAR(fn(1.0), 1.0, 1e-9);
    }
}

TEST(easing_in_out_curves_are_continuous_at_the_half)
{
    for (easing_functions which : kInOut)
    {
        const easingFunction fn = getEasingFunction(which);
        CHECK_NEAR(fn(0.5), 0.5, 1e-9);
        CHECK_NEAR(fn(0.5 - 1e-6), fn(0.5 + 1e-6), 1e-4);
    }
}
