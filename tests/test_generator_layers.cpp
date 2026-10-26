// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include "eanim/generator_hsv.h"

using namespace ecore;
using namespace eanim;

namespace
{
    // paints a solid, whatever is beneath
    struct Solid : public GeneratorHSV
    {
        HSV color;
        int ticks{0};
        explicit Solid(const HSV& inColor) : color(inColor) {}
        void render(HSVStripNode*, HSV& inOutColor) const override { inOutColor = color; }
        void tick(float) override { ++ticks; }
    };

    // halves the brightness of what it is given: a look that joins
    struct Dim : public GeneratorHSV
    {
        int ticks{0};
        void render(HSVStripNode*, HSV& inOutColor) const override
        {
            inOutColor.setBrightnessAlpha(inOutColor.getValFloat() * 0.5f);
        }
        void tick(float) override { ++ticks; }
    };
}

TEST(layers_render_bottom_first_and_pass_the_colour_up)
{
    auto solid = std::make_shared<Solid>(HSV(120.0f, 1.0f, 1.0f));
    auto dim = std::make_shared<Dim>();

    LayeredGeneratorHSV stack;
    stack.addLayer(solid);
    stack.addLayer(dim);

    HSV color; // black beneath, as the render loop hands it
    stack.render(nullptr, color);
    CHECK_NEAR(color.getHueFloat(), 120.0f, 0.1f);
    CHECK_NEAR(color.getValFloat(), 0.5f, 0.001f);

    // the other way up, the solid paints over the dim's work
    LayeredGeneratorHSV reversed;
    reversed.addLayer(dim);
    reversed.addLayer(solid);
    HSV again;
    reversed.render(nullptr, again);
    CHECK_NEAR(again.getValFloat(), 1.0f, 0.001f);
}

TEST(layers_tick_every_layer_and_tolerate_empty)
{
    auto solid = std::make_shared<Solid>(HSV());
    auto dim = std::make_shared<Dim>();

    LayeredGeneratorHSV stack;
    stack.addLayer(solid);
    stack.addLayer(dim);
    stack.addLayer(nullptr); // ignored, not a crash
    CHECK_EQ(stack.getLayers().size(), 2u);

    stack.tick(0.016f);
    stack.tick(0.016f);
    CHECK_EQ(solid->ticks, 2);
    CHECK_EQ(dim->ticks, 2);

    LayeredGeneratorHSV empty;
    HSV untouched(30.0f, 0.5f, 0.5f);
    empty.render(nullptr, untouched);
    CHECK_NEAR(untouched.getHueFloat(), 30.0f, 0.1f);
    empty.tick(0.016f);
}
