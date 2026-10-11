// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "recording_patterns.h"

#include <algorithm>
#include <cmath>

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

namespace
{
    /// a fixed 0..1 per node, so a firefly or a light keeps its own clock
    float nodeHash(const HSVStripNode* node, const Coordinate& at)
    {
        const float seed = static_cast<float>(node->getStripIdx()) * 12.9898f + at.x * 78.233f + at.y * 37.719f;
        const float h = std::sin(seed) * 43758.5453f;
        return h - std::floor(h);
    }

    float unit(float value) { return std::clamp(value, 0.0f, 1.0f); }
}

// ============================================================================
// secret_alien
// ============================================================================

void Pattern_Scanner_SecretAlien::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const NodeSpace space = eio::nodeSpace(inNode);

    // the transmission stutters: a slow beat of two sines that dips now and
    // then rather than on a rhythm anyone could count
    const float wobble = std::sin(timeActive * 7.3f) * std::sin(timeActive * 2.9f + 1.3f);
    const float signal = 1.0f - stutter * unit(wobble * 3.0f - 2.0f);

    if (space == NodeSpace::Ring)
    {
        // the saucer: running lights round the rim, green and magenta by
        // turns. Round by bearing from the ring's centre rather than along
        // the strip - on a desk the ring is a short run of a long one
        const Coordinate at = nodeCoord(inNode);
        const float bearing = std::atan2(at.x - kRingCenterX, at.y - kRingCenterY) / 6.2832f + 0.5f;
        const float count = std::max(1.0f, std::round(lights));
        const float around = (bearing - timeActive * orbitRate) * count;
        const float slot = around - std::floor(around);
        const float lit = std::pow(std::max(0.0f, 1.0f - std::fabs(slot - 0.5f) * 3.0f), 2.0f);
        const int which = static_cast<int>(std::floor(around));
        HSV light = (((which % 2) + 2) % 2 == 0) ? lightA : lightB;
        inOutColor = HSV::blend(hullColor, light, lit * signal);
        return;
    }

    // the beam: bands falling from the tip, a faint glow between them
    float phase = stageAlpha(inNode) * bands + timeActive * fallRate;
    phase -= std::floor(phase);
    const float band = phase * phase * phase;
    const float level = (glow + (1.0f - glow) * band) * signal;

    inOutColor = beamColor;
    inOutColor.setBrightnessAlpha(beamColor.getValFloat() * unit(level));
}

void Pattern_Scanner_SecretAlien::reflect(ecore::PropertyBag& bag)
{
    bag.add("beam", beamColor);
    bag.add("hull", hullColor);
    bag.add("light_a", lightA);
    bag.add("light_b", lightB);
    bag.add("lights", lights, 1.0f, 12.0f);
    bag.add("orbit_rate", orbitRate, 0.0f, 2.0f);
    bag.add("bands", bands, 1.0f, 10.0f);
    bag.add("fall_rate", fallRate, 0.0f, 3.0f);
    bag.add("glow", glow, 0.0f, 1.0f);
    bag.add("stutter", stutter, 0.0f, 1.0f);
}

// ============================================================================
// secret_frog
// ============================================================================

void Pattern_Scanner_SecretForest::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);
    const float t = timeActive * drift;

    // the canopy: two slow sines crossed over the stage, moss where they
    // agree and pine where they do not
    const float canopy = 0.5f + 0.5f * std::sin(at.x * 0.9f + t * 1.3f) * std::sin(at.y * 0.22f - t);
    HSV color = HSV::blend(pineColor, mossColor, canopy);

    // a light shaft wandering across the stage, lifting what it falls on
    const float shaftX = 3.5f + 5.0f * std::sin(t * 0.7f);
    const float lit = std::max(0.0f, 1.0f - std::fabs(at.x + 0.15f * at.y - shaftX) / 2.5f);
    color.setBrightnessAlpha(unit(color.getValFloat() * (1.0f + shaft * 2.0f * lit * lit)));

    // fireflies: every pixel its own, on its own clock, winking briefly
    const float seed = nodeHash(inNode, at);
    const float wink = std::sin(timeActive * fireflyRate * (0.6f + 0.8f * seed) * 6.2832f + seed * 6.2832f);
    const float firefly = std::pow(std::max(0.0f, wink), std::max(fireflies, 1.0f));

    inOutColor = HSV::blend(color, fireflyColor, firefly);
}

void Pattern_Scanner_SecretForest::reflect(ecore::PropertyBag& bag)
{
    bag.add("moss", mossColor);
    bag.add("pine", pineColor);
    bag.add("firefly", fireflyColor);
    bag.add("drift", drift, 0.0f, 2.0f);
    bag.add("firefly_rate", fireflyRate, 0.05f, 3.0f);
    bag.add("fireflies", fireflies, 1.0f, 40.0f);
    bag.add("shaft", shaft, 0.0f, 1.0f);
}
