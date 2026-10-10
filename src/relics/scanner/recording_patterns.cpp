// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "recording_patterns.h"

#include <algorithm>

// HSVStripNode_Space, for the obelisk's nodes
#include "../../lib/eio/strip_projection.h"

using namespace scanner;

void Pattern_Scanner_ArmFire::reset()
{
    Pattern_Generic_Fire2012::reset();
    doused = false;
    sinceDouse = 0.0f;
    emitter = 1.0f;
}

bool Pattern_Scanner_ArmFire::onTrigger(const GameplayTag& tag)
{
    if (tag != scanner_tags::Douse)
    {
        return Pattern_Generic_Fire2012::onTrigger(tag);
    }

    doused = true;
    sinceDouse = 0.0f;
    return true;
}

void Pattern_Scanner_ArmFire::tick(float deltaTime)
{
    // the emitter runs down over the douse and stays down: the flames
    // already in the air burn out on their own clocks, so the fire dies
    // from the foot up rather than going out at once
    if (doused)
    {
        sinceDouse += deltaTime;
        emitter = 1.0f - std::clamp(sinceDouse / std::max(douseSeconds, 0.01f), 0.0f, 1.0f);
    }

    Pattern_Generic_Fire2012::tick(deltaTime);
}

void Pattern_Scanner_ArmFire::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    HSV fire;
    Pattern_Generic_Fire2012::render(inNode, fire);

    // over the sculpture's own picture, by the fire's own heat: dark flame
    // is no claim at all, and the tower shows through
    const HSVStripNode_Space* spaced = eio::spaceOf(inNode);
    if (spaced != nullptr && spaced->space == NodeSpace::Obelisk)
    {
        HSV under;
        if (underlay != nullptr && underlay->sample(inNode, under))
        {
            inOutColor = HSV::blend(under, fire, fire.getValFloat());
            return;
        }
    }

    inOutColor = fire;
}

void Pattern_Scanner_ArmFire::reflect(ecore::PropertyBag& bag)
{
    Pattern_Generic_Fire2012::reflect(bag);
    bag.add("douse", douseSeconds, 0.05f, 2.0f);
}

Pattern_Scanner_SecretRain::Pattern_Scanner_SecretRain()
    : Pattern_Generic_MatrixRain(48)
{
    // the old ramp's colours: orange (30) through yellow to green (120)
    baseHue = 30.0f;
    hueSpread = 0.25f;
    dropCount = 30.0f;
    flicker = 0.3f;
}

void Pattern_Scanner_SecretRain::reset()
{
    Pattern_Generic_MatrixRain::reset();

    // Roll the storm forward until the first drops have crossed the stage,
    // as the Mythos rain does: spawned up to tail + 20 above the top and
    // falling at 0.6..1.4 of fall_speed. Small steps, so the particles
    // integrate the path they would have under real frames.
    const float span = tailLength + 20.0f + (kStageTop - kStageBottom);
    const float seconds = span / std::max(0.6f * fallSpeed, 0.1f);
    const float step = 1.0f / 30.0f;
    for (float rolled = 0.0f; rolled < seconds; rolled += step)
    {
        Pattern_Generic_MatrixRain::tick(step);
    }
    // the clock is the cue's: the churn hashes off it
    timeActive = 0.0f;
}
