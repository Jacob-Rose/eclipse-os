// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "recording_patterns.h"

#include <algorithm>

// HSVStripNode_Space, for the obelisk's nodes
#include "../../lib/eio/strip_projection.h"

using namespace scanner;

void Pattern_Scanner_RecordArm::reset()
{
    Pattern_Generic_Fire2012::reset();
    doused = false;
    sinceDouse = 0.0f;
    emitter = 1.0f;
}

bool Pattern_Scanner_RecordArm::onTrigger(const GameplayTag& tag)
{
    if (tag != scanner_tags::Douse)
    {
        return Pattern_Generic_Fire2012::onTrigger(tag);
    }

    doused = true;
    sinceDouse = 0.0f;
    return true;
}

void Pattern_Scanner_RecordArm::tick(float deltaTime)
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

void Pattern_Scanner_RecordArm::render(HSVStripNode* inNode, HSV& inOutColor) const
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

void Pattern_Scanner_RecordArm::reflect(ecore::PropertyBag& bag)
{
    Pattern_Generic_Fire2012::reflect(bag);
    bag.add("douse", douseSeconds, 0.05f, 2.0f);
}
