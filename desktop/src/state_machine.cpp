// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/state_machine.h"

#include <algorithm>

#include "lib/ecore/coord.h"

// The jacket's looks, compiled for the host. Same files the jacket runs.
#include "relics/jacket/jacket_io.h"
#include "relics/jacket/visual/state_bluemagic.h"
#include "relics/jacket/visual/state_breathewithme.h"
#include "relics/jacket/visual/state_campfire.h"
#include "relics/jacket/visual/state_cybertoxin.h"
#include "relics/jacket/visual/state_datamine.h"
#include "relics/jacket/visual/state_digitalvoid.h"
#include "relics/jacket/visual/state_enchantedforest.h"
#include "relics/jacket/visual/state_hitstop.h"
#include "relics/jacket/visual/state_parrot.h"
#include "relics/jacket/visual/state_rainbowroad.h"
#include "relics/jacket/visual/state_systemoverload.h"
#include "relics/jacket/visual/state_warpturbines.h"

using namespace edmx;

// ============================================================================
// HostRelicIO
// ============================================================================

void HostRelicIO::build(size_t count, uint8_t segmentId, const CoordFrame& frame,
                        const std::vector<float>& positions)
{
    strip_segments.clear();
    strips.clear();
    nodes.clear();

    strip.reset(new eio::HSVStrip(static_cast<uint16_t>(count), 0));

    auto segment = std::make_unique<eio::HSVStripSegment>(strip.get(), segmentId);

    for (size_t idx = 0; idx < count; ++idx)
    {
        auto node = std::make_shared<eio::HSVStripNode_Mapped2D>(segment.get(), static_cast<int>(idx));

        const float position = (idx < positions.size()) ? positions[idx] : 0.0f;
        node->coord = frame.at(position);

        nodes.push_back(node);
        segment->addNode(node);
    }

    strip_segments.emplace(segmentId, std::move(segment));

    // Full brightness here; the rig's own master and gamma are applied further
    // down the chain, and dimming twice would just cost resolution.
    setGlobalBrightness(eio::EBrightness::MAX);
}

// ============================================================================
// StateMachinePattern
// ============================================================================

StateMachinePattern::StateMachinePattern(const char* inName, std::vector<StateDef> inStates,
                                         uint8_t inSegmentId, const CoordFrame& inFrame,
                                         float inTransitionTime)
    : name(inName), states(std::move(inStates)), segmentId(inSegmentId),
      frame(inFrame), transitionTime(inTransitionTime)
{
}

void StateMachinePattern::ensureBuilt(const PatternContext& context)
{
    if (io && builtFor == context.fixtureCount)
    {
        return;
    }
    builtFor = context.fixtureCount;

    io.reset(new HostRelicIO());
    io->build(context.fixtureCount, segmentId, context.coords, context.positions);

    manager.reset(new esm::StateManager());
    machine.reset(new StateMachine_GenericHSV());
    machine->transitionTime = transitionTime;
    machine->setRelicIO(io.get());

    instances.clear();
    generators.clear();

    for (const StateDef& def : states)
    {
        auto generator = def.make();

        // State_GenericHSV rather than each look's own State_ subclass: those
        // wrappers reach into JacketIO for physical buttons, which do not exist
        // here. The state machine, the transitions and the pattern code are all
        // the relic's own; only where the input comes from differs.
        auto state = std::make_shared<State_GenericHSV>(def.name.c_str(), io.get());
        state->setGenerator(generator);
        state->init();

        manager->addState(state);
        instances.push_back(state);
        generators.push_back(generator);
    }

    if (!instances.empty())
    {
        activeIndex = std::min(activeIndex, instances.size() - 1);

        // Straight to active, no fade: there is nothing to fade from, and
        // StateMachine::tick dereferences the active state unconditionally.
        machine->setActiveState(instances[activeIndex]);
        machine->init();
    }
}

void StateMachinePattern::tick(float deltaTime)
{
    // Banked, not spent. The machine renders through nodes, and the nodes are
    // not built until render() hands us the rig's shape; ticking here would
    // mean ticking into nothing on the first frame.
    pendingDelta += deltaTime;
}

void StateMachinePattern::render(const PatternContext& context, std::vector<ecore::HSV>& outColors)
{
    ensureBuilt(context);

    outColors.assign(context.fixtureCount, ecore::HSV(0.0f, 0.0f, 0.0f));
    if (!machine || instances.empty())
    {
        return;
    }

    // Feed the momentary inputs to whichever looks take them, then let the
    // machine run: states write through their nodes into the strip, and a
    // transition blends two states' buffers into it.
    //
    // Applied to every state, not just the active one, because a cross-fade
    // renders two of them and a look arriving mid-fade with a stale input
    // would be visible.
    for (size_t idx = 0; idx < states.size(); ++idx)
    {
        if (states[idx].applyInput && generators[idx])
        {
            states[idx].applyInput(generators[idx].get(), inputA, inputB);
        }
    }

    machine->tick(pendingDelta);
    pendingDelta = 0.0f;

    eio::HSVStrip* strip = io->getStrip();
    for (size_t idx = 0; idx < context.fixtureCount; ++idx)
    {
        ecore::HSV color = strip->getHSV(static_cast<uint16_t>(idx));
        color.setBrightnessAlpha(color.getValFloat() * brightness);
        outColors[idx] = color;
    }
}

std::vector<std::string> StateMachinePattern::stateNames() const
{
    std::vector<std::string> names;
    names.reserve(states.size());
    for (const StateDef& def : states)
    {
        names.push_back(def.name);
    }
    return names;
}

std::string StateMachinePattern::currentStateName() const
{
    if (states.empty())
    {
        return std::string();
    }
    return states[std::min(activeIndex, states.size() - 1)].name;
}

void StateMachinePattern::reflect(ecore::PropertyBag& bag)
{
    // The showing look's knobs, and only its. Empty before the first render,
    // because the generators do not exist until the rig's shape is known - see
    // ensureBuilt. main.cpp announces the set once a frame has been drawn for
    // exactly that reason.
    if (activeIndex < generators.size() && generators[activeIndex])
    {
        generators[activeIndex]->reflect(bag);
    }
}

bool StateMachinePattern::setState(const std::string& stateName, std::string& outError)
{
    size_t target = states.size();
    for (size_t idx = 0; idx < states.size(); ++idx)
    {
        if (states[idx].name == stateName)
        {
            target = idx;
            break;
        }
    }

    if (target == states.size())
    {
        std::string known;
        for (const StateDef& def : states)
        {
            known += (known.empty() ? "" : ", ") + def.name;
        }
        outError = "no state '" + stateName + "' (have: " + known + ")";
        return false;
    }

    if (target == activeIndex)
    {
        return true; // already there; asking twice is not a failure
    }

    activeIndex = target;

    // Before the first render the machine does not exist yet; activeIndex is
    // enough, and ensureBuilt will start on it.
    if (machine && target < instances.size())
    {
        machine->setNextState(instances[target]);
    }
    return true;
}

void StateMachinePattern::setInput(bool inA, bool inB)
{
    inputA = inA;
    inputB = inB;
}

// ============================================================================
// The jacket
// ============================================================================

namespace
{
    /// A look with no input.
    template <typename PatternT>
    StateDef look(const char* name)
    {
        StateDef def;
        def.name = name;
        def.make = []() {
            auto pattern = std::make_shared<PatternT>();
            pattern->init();
            return std::static_pointer_cast<eanim::GeneratorHSV>(pattern);
        };
        return def;
    }

    /// A look that reads the two momentary inputs.
    ///
    /// Every jacket pattern that takes input spells it the same way, as two
    /// public bools, so one template covers all of them.
    template <typename PatternT>
    StateDef interactiveLook(const char* name)
    {
        StateDef def = look<PatternT>(name);
        def.applyInput = [](eanim::GeneratorHSV* generator, bool inputA, bool inputB) {
            auto* pattern = static_cast<PatternT*>(generator);
            pattern->bIsButtonAActive = inputA;
            pattern->bIsButtonBActive = inputB;
        };
        return def;
    }
}

std::unique_ptr<StateMachinePattern> edmx::makeJacketStateMachine()
{
    // ------------------------------------------------------------------
    // The looks. Adding one is a single line.
    //
    //   look<Pattern_X>("name")             - no input
    //   interactiveLook<Pattern_X>("name")  - reads the two inputs
    //
    // Order is the order a UI shows them in.
    // ------------------------------------------------------------------
    std::vector<StateDef> states = {
        interactiveLook<Pattern_DigitalVoid>("digital_void"),
        interactiveLook<Pattern_EnchantedForest>("enchanted_forest"),
        interactiveLook<Pattern_Jacket_WarpTurbines>("warp_turbines"),
        interactiveLook<Pattern_Jacket_RainbowRoad>("rainbow_road"),
        interactiveLook<Pattern_BreatheWithMe>("breathe_with_me"),
        interactiveLook<Pattern_Parrot>("parrot"),
        interactiveLook<Pattern_SystemOverload>("system_overload"),
        interactiveLook<Pattern_CyberToxin>("cyber_toxin"),
        look<Pattern_Datamine>("datamine"),
        look<Pattern_BlueMagic>("blue_magic"),
        look<Pattern_Campfire>("campfire"),
        look<Pattern_Hitstop>("hitstop"),
    };

    // ------------------------------------------------------------------
    // The rig pretends to be the jacket's monowire.
    //
    // Jacket looks branch on which segment a node belongs to, and the monowire
    // - the whip - is the one that is a single long run rather than a ring
    // around a sleeve. That is the branch a line of pars wants, and it reads
    // coord.y, so the rig runs up the y axis.
    //
    // The whip itself is 52 LEDs from y=-0.25 across about 8.75 units, roughly
    // 0.17 per pixel. Ten pars over that same extent land about 0.88 apart:
    // the spacing opens up by roughly 5x, which is the point. A rig is not a
    // dense strip, and squeezing it into the whip's pixel pitch would make
    // every look read as one flat wash.
    //
    // Dial it with pattern.coord_span_y if the rig wants more or less travel.
    // ------------------------------------------------------------------
    CoordFrame whip;
    whip.originX = 0.75f;
    whip.spanX   = 0.10f;
    whip.originY = -0.25f;
    whip.spanY   = 8.75f;

    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "jacket", std::move(states),
        static_cast<uint8_t>(jacket::JacketSegmentID::MONOWIRE),
        whip,
        0.4f)); // the jacket's own cross-fade
}
