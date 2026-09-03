// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/relic_shadow.h"

#include "lib/ecore/property.h"
#include "lib/esm/state.h"
#include "lib/esm/state_generic.h"

using namespace edmx;

RelicShadow::RelicShadow() = default;

RelicShadow::~RelicShadow()
{
    clear();
}

void RelicShadow::setProfile(const RelicShadowProfile* inProfile)
{
    if (inProfile != profile)
    {
        clear();
    }
    profile = inProfile;
}

void RelicShadow::clear()
{
    // The machine holds the state which holds the look; the io underneath
    // them all. Torn down in that order.
    machine.reset();
    manager.reset();
    state.reset();
    look.reset();
    io.reset();
    lookName.clear();
}

bool RelicShadow::spawn(const std::string& name, std::string& outError)
{
    if (profile == nullptr || !profile->makeIO || !profile->makeLook)
    {
        outError = "no shadow profile for this relic";
        clear();
        return false;
    }

    std::shared_ptr<eanim::GeneratorHSV> made = profile->makeLook(name);
    if (!made)
    {
        outError = "the " + profile->relic + " profile has no look called '" + name + "'";
        clear();
        return false;
    }

    clear();

    // The relic's own geometry rather than the show's patch of it, so the
    // look reads exactly the nodes it reads on the board.
    io = profile->makeIO();
    if (!io)
    {
        outError = "the " + profile->relic + " profile built no IO";
        clear();
        return false;
    }

    look = made;
    lookName = name;

    state = std::make_shared<State_GenericHSV>(name.c_str(), io.get());
    state->setGenerator(look);
    state->init();

    manager.reset(new esm::StateManager());
    manager->addState(state);

    machine.reset(new StateMachine_GenericHSV());
    machine->setRelicIO(io.get());
    machine->setActiveState(state);
    machine->init();
    return true;
}

bool RelicShadow::applySim(const std::string& text, std::string& outError)
{
    std::string name = text;
    std::string values;
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
    {
        name.erase(name.begin());
    }
    const size_t space = name.find(' ');
    if (space != std::string::npos)
    {
        values = name.substr(space + 1);
        name = name.substr(0, space);
    }
    while (!name.empty() && (name.back() == '\r' || name.back() == '\n'))
    {
        name.pop_back();
    }

    if (name.empty())
    {
        outError = "sim named no look";
        clear();
        return false;
    }

    if (!isLive() || name != lookName)
    {
        if (!spawn(name, outError))
        {
            return false;
        }
    }

    ecore::PropertyBag bag;
    state->reflectState(bag);
    ecore::applyState(bag, values);
    syncedAt = std::chrono::steady_clock::now();

    // Draw it at that state now, so a sample this frame is the relic's
    // picture rather than whatever the strip held.
    tick(0.0f);
    return true;
}

void RelicShadow::tick(float deltaTime)
{
    if (machine)
    {
        machine->tick(deltaTime);
    }
}

double RelicShadow::sinceSync() const
{
    if (!isLive())
    {
        return 0.0;
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - syncedAt).count();
}

std::string RelicShadow::describeState() const
{
    if (!state)
    {
        return std::string();
    }
    ecore::PropertyBag bag;
    state->reflectState(bag);
    return ecore::serializeState(bag);
}

const eio::HSVStrip* RelicShadow::firstStrip() const
{
    if (!io || io->strips.empty())
    {
        return nullptr;
    }
    return io->strips.begin()->second.get();
}

uint16_t RelicShadow::pixelCount() const
{
    const eio::HSVStrip* strip = firstStrip();
    return strip ? strip->getLength() : 0;
}

ecore::HSV RelicShadow::colorAt(uint16_t stripIdx) const
{
    const eio::HSVStrip* strip = firstStrip();
    if (strip == nullptr || stripIdx >= strip->getLength())
    {
        return ecore::HSV(0.0f, 0.0f, 0.0f);
    }
    return strip->getHSV(stripIdx);
}

bool RelicShadow::sample(const eio::HSVStripNode* node, ecore::HSV& outColor) const
{
    if (!isLive() || profile == nullptr)
    {
        return false;
    }
    const eio::HSVStripNode_Space* spaced = eio::spaceOf(node);
    if (spaced == nullptr || spaced->space != profile->space)
    {
        return false;
    }
    // The node's index within its object is its place on the relic's strip:
    // a device file lists its fixtures in strip order, the order the strip
    // is wired in.
    if (spaced->index < 0 || spaced->index >= static_cast<int>(pixelCount()))
    {
        return false;
    }
    outColor = colorAt(static_cast<uint16_t>(spaced->index));
    return true;
}
