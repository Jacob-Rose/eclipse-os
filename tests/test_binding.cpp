// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <ecore/binding.h>
#include <ecore/property.h>
#include <ecore/reflectable.h>

using namespace ecore;

namespace
{
    struct Look : public Reflectable
    {
        float speed{1.0f};
        bool bFlip{false};
        HSV tint;
        void reflect(PropertyBag& bag) override
        {
            bag.add("speed", speed, 2.0f, 10.0f);
            bag.add("flip", bFlip);
            bag.add("tint", tint);
        }
    };
}

TEST(binding_lands_a_unit_value_in_the_propertys_own_range)
{
    Look look;
    PropertyBag bag;
    look.reflect(bag);

    CHECK(landUnit(bag, "speed", 0.5f));
    CHECK_NEAR(look.speed, 6.0f, 1e-6f);      // halfway across 2..10
    CHECK(landUnit(bag, "speed", 1.0f));
    CHECK_NEAR(look.speed, 10.0f, 1e-6f);

    CHECK(landUnit(bag, "flip", 0.5f));       // the threshold is a half
    CHECK(look.bFlip);
    CHECK(landUnit(bag, "flip", 0.49f));
    CHECK(!look.bFlip);

    CHECK(!landUnit(bag, "tint", 0.5f));      // a colour is not one number
    CHECK(!landUnit(bag, "missing", 0.5f));
}

TEST(binding_rule_scales_rate_limits_and_edge_triggers)
{
    BindingRule level;
    level.low = 0.0f;
    level.high = 2.0f;
    level.minInterval = 0.1;
    level.minChange = 0.05;
    level.maxInterval = 0.5;

    float value = 0.0f;
    CHECK(level.takes(1.0f, 1.0, value));
    CHECK_NEAR(value, 0.5f, 1e-6f);
    CHECK(!level.takes(1.9f, 1.05, value));   // inside the interval
    CHECK(!level.takes(1.02f, 1.2, value));   // too small a change
    CHECK(level.takes(1.02f, 1.6, value));    // the keepalive restates it
    CHECK_NEAR(level.scale(-5.0f), 0.0f, 0.0f);
    CHECK_NEAR(level.scale(9.0f), 1.0f, 0.0f);
    level.reset();
    CHECK(level.takes(1.02f, 1.61, value));   // forgotten: acts at once

    BindingRule beat;
    beat.mode = BindingRule::Mode::Trigger;
    CHECK(!beat.takes(0.2f, 0.0, value));
    CHECK(beat.takes(0.9f, 0.01, value));     // the rising edge
    CHECK_NEAR(value, 1.0f, 0.0f);
    CHECK(!beat.takes(0.95f, 0.02, value));   // still up
    CHECK(!beat.takes(0.1f, 0.03, value));
    CHECK(beat.takes(0.7f, 0.04, value));

    BindingRule::Mode mode;
    CHECK(BindingRule::parseMode("trigger", mode) && mode == BindingRule::Mode::Trigger);
    CHECK(!BindingRule::parseMode("edge", mode));
    CHECK_EQ(std::string(BindingRule::modeName(BindingRule::Mode::Value)), std::string("value"));

    BindingRule flat;
    flat.high = flat.low;
    CHECK_NEAR(flat.scale(3.0f), 0.0f, 0.0f);   // an empty range is nothing, not a division
}
