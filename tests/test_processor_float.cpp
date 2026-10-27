// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <cmath>

#include <eanim/processor_float.h>

using namespace eanim;

TEST(lerper_moves_in_a_straight_line_and_retargets_from_where_it_is)
{
    Lerper lerp(0.0f);
    CHECK(!lerp.isLerping());
    CHECK_NEAR(lerp.getValue(), 0.0f, 0.0f);

    lerp.startLerp(1.0f, 2.0f);
    CHECK(lerp.isLerping());
    lerp.tick(0.5f);
    CHECK_NEAR(lerp.getValue(), 0.25f, 1e-6f);
    CHECK_NEAR(lerp.getProgress(), 0.25f, 1e-6f);

    // a new target from the quarter mark, no jump
    lerp.setTargetValue(0.0f);          // reuses the 2s duration
    CHECK_NEAR(lerp.getValue(), 0.25f, 1e-6f);
    lerp.tick(1.0f);
    CHECK_NEAR(lerp.getValue(), 0.125f, 1e-6f);
    lerp.tick(10.0f);                   // overshooting time clamps at the target
    CHECK_NEAR(lerp.getValue(), 0.0f, 1e-6f);
    CHECK(!lerp.isLerping());

    // zero duration snaps, and never divides by zero
    lerp.startLerp(5.0f, 0.0f);
    CHECK(!lerp.isLerping());
    CHECK_NEAR(lerp.getValue(), 5.0f, 0.0f);
    CHECK(!std::isnan(lerp.getProgress()));

    lerp.reset(-1.0f);
    CHECK_NEAR(lerp.getValue(), -1.0f, 0.0f);
}

TEST(momentum_respects_the_speed_limit_and_lands_on_the_target)
{
    Momentum head(2.0f, 1.0f);           // 2 units/s^2, 1 unit/s
    head.reset(0.0f);
    head.setTargetValue(10.0f);

    float peak = 0.0f;
    float last = 0.0f;
    float arrivedAt = -1.0f;
    for (int frame = 0; frame < 2000; ++frame)
    {
        head.tick(0.01f);
        peak = std::max(peak, std::fabs(head.getSpeed()));
        CHECK(head.getValue() >= last - 1e-5f);   // monotonic: never overshoots and comes back
        CHECK(head.getValue() <= 10.0f + 1e-5f);
        last = head.getValue();
        if (arrivedAt < 0.0f && head.isSettled()) arrivedAt = frame * 0.01f;
    }
    CHECK(peak <= 1.0f + 1e-5f);
    CHECK(head.isSettled());
    CHECK_NEAR(head.getValue(), 10.0f, 0.0f);
    // 0.5s to reach speed, 10.25 units of cruise... about 11s all in
    CHECK(arrivedAt > 10.0f && arrivedAt < 12.0f);

    // the other way, and a retarget mid-flight turns around cleanly
    head.setTargetValue(0.0f);
    for (int frame = 0; frame < 300; ++frame) head.tick(0.01f);
    CHECK(head.getSpeed() < 0.0f);
    head.setTargetValue(10.0f);
    for (int frame = 0; frame < 2000; ++frame) head.tick(0.01f);
    CHECK(head.isSettled());
    CHECK_NEAR(head.getValue(), 10.0f, 0.0f);

    // no acceleration means no way to move gradually: it just arrives
    Momentum instant(0.0f, 1.0f);
    instant.setTargetValue(3.0f);
    instant.tick(0.01f);
    CHECK_NEAR(instant.getValue(), 3.0f, 0.0f);
}

namespace
{
    // runs a spring to rest and reports the furthest it went past the target
    float overshootOf(Spring& spring, float target, float dt, int frames)
    {
        float overshoot = 0.0f;
        spring.setTargetValue(target);
        for (int frame = 0; frame < frames; ++frame)
        {
            spring.tick(dt);
            overshoot = std::max(overshoot, spring.getValue() - target);
        }
        return overshoot;
    }
}

TEST(spring_settles_and_only_bounces_when_underdamped)
{
    Spring critical(100.0f, 1.0f);
    critical.reset(0.0f);
    CHECK_NEAR(overshootOf(critical, 1.0f, 1.0f / 60.0f, 600), 0.0f, 1e-3f);
    CHECK(critical.isSettled());
    CHECK_NEAR(critical.getValue(), 1.0f, 1e-3f);

    Spring bouncy(100.0f, 0.3f);
    bouncy.reset(0.0f);
    const float bounce = overshootOf(bouncy, 1.0f, 1.0f / 60.0f, 600);
    CHECK(bounce > 0.2f);                 // visibly past the target...
    CHECK(bouncy.isSettled());            // ...and still at rest in the end

    // a stiff spring on a slow frame does not blow up
    Spring stiff(4000.0f, 0.5f);
    stiff.reset(0.0f);
    stiff.setTargetValue(1.0f);
    for (int frame = 0; frame < 60; ++frame) stiff.tick(0.1f);
    CHECK(std::isfinite(stiff.getValue()));
    CHECK(stiff.isSettled(0.01f));

    // no stiffness is a literal
    Spring limp(0.0f);
    limp.setTargetValue(2.0f);
    limp.tick(0.016f);
    CHECK_NEAR(limp.getValue(), 2.0f, 0.0f);
    CHECK_NEAR(limp.getVelocity(), 0.0f, 0.0f);
}
