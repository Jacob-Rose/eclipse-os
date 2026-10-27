// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <ecore/property.h>
#include <ecore/reflectable.h>
#include <eanim/automation_curve.h>

using namespace ecore;

namespace
{
    // a child that reflects on its own, the way an LFO or a noise field does
    struct Clock : public Reflectable
    {
        float time{1.5f};
        void reflect(PropertyBag& bag) override { bag.addState("time", time); }
    };

    struct Look : public Reflectable
    {
        float gain{0.5f};
        bool bInverted{false};
        HSV tint{120.0f, 1.0f, 1.0f};
        eanim::AutomationCurve envelope;
        Clock clock;
        int rebuilds{0};

        void reflect(PropertyBag& bag) override
        {
            bag.add("gain", gain, 0.0f, 2.0f, [this] { ++rebuilds; });
            bag.add("inverted", bInverted);
            bag.add("tint", tint);
            bag.add("envelope", envelope);
            bag.reflect("clock", clock);
        }
    };
}

TEST(property_bag_gathers_every_type_by_name)
{
    Look look;
    PropertyBag bag;
    look.reflect(bag);

    CHECK_EQ(bag.size(), 5u);
    CHECK(bag.find("gain") != nullptr);
    CHECK(bag.find("gain")->type == Property::Type::Float);
    CHECK(bag.find("inverted")->type == Property::Type::Bool);
    CHECK(bag.find("tint")->type == Property::Type::Color);
    CHECK(bag.find("envelope")->type == Property::Type::Curve);
    CHECK(bag.find("envelope")->curveValue == &look.envelope);
    CHECK(bag.find("missing") == nullptr);
}

TEST(property_set_clamps_floats_and_fires_changed)
{
    Look look;
    PropertyBag bag;
    look.reflect(bag);

    CHECK(bag.set("gain", 1.25f));
    CHECK_NEAR(look.gain, 1.25f, 1e-6f);
    CHECK(bag.set("gain", 9.0f));
    CHECK_NEAR(look.gain, 2.0f, 1e-6f);
    CHECK_EQ(look.rebuilds, 2);

    CHECK(bag.set("inverted", 1.0f));
    CHECK(look.bInverted);
    CHECK_NEAR(bag.find("inverted")->get(), 1.0f, 0.0f);

    CHECK(!bag.set("missing", 1.0f));
}

TEST(property_colour_is_one_value_not_a_number)
{
    Look look;
    PropertyBag bag;
    look.reflect(bag);

    CHECK(bag.setColor("tint", HSV(0.0f, 1.0f, 0.5f)));
    CHECK_NEAR(look.tint.getHueFloat(), 0.0f, 0.05f);
    CHECK(!bag.setColor("gain", HSV()));          // a swatch on a float is refused
    CHECK(bag.set("tint", 3.0f));                  // found by name...
    CHECK_NEAR(look.tint.getValFloat(), 0.5f, 0.001f); // ...but a number does nothing to it
}

TEST(property_children_nest_under_a_prefix)
{
    Look look;
    PropertyBag bag;
    look.reflect(bag);

    const Property* time = bag.find("clock.time");
    CHECK(time != nullptr);
    CHECK(time != nullptr && time->isState());
    CHECK(bag.find("time") == nullptr);
    CHECK(bag.find("gain")->isKnob());   // the prefix was popped afterwards
}

TEST(property_state_round_trips_as_text_and_skips_knobs)
{
    Look a;
    PropertyBag bagA;
    a.reflect(bagA);
    a.clock.time = 42.125f;
    a.gain = 1.75f;

    const std::string text = serializeState(bagA);
    CHECK_EQ(text, std::string("clock.time=42.125"));

    Look b;
    PropertyBag bagB;
    b.reflect(bagB);
    CHECK_EQ(applyState(bagB, "  clock.time=42.125   nothing.here=3 junk gain=1.75 "), 2);
    CHECK_NEAR(b.clock.time, 42.125f, 0.0f);
    CHECK_NEAR(b.gain, 1.75f, 0.0f);   // a named knob still lands; it is only *emitted* as state
}

TEST(property_duplicate_names_are_refused)
{
    float x = 0.0f;
    float y = 0.0f;
    PropertyBag bag;
    bag.add("same", x, 0.0f, 1.0f);
    bag.add("same", y, 0.0f, 1.0f);

    CHECK_EQ(bag.size(), 1u);
    bag.set("same", 1.0f);
    CHECK_NEAR(x, 1.0f, 0.0f);
    CHECK_NEAR(y, 0.0f, 0.0f);
}
