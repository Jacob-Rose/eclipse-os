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

// The scanner's looks - afterglow's LED states. Same files the obelisk runs.
#include "relics/scanner/scanner_patterns.h"

// The generic looks - free-standing stage patterns, grouped as a machine.
#include "relics/scanner/generic_patterns.h"
// the recording flow's looks built on a generic one: cleanse_arm's fire
#include "relics/scanner/recording_patterns.h"

// The obelisk's own looks, for their machine and a borrowed generic cue.
#include "relics/obelisk/state_obelisk.h"

using namespace edmx;

// ============================================================================
// HostRelicIO
// ============================================================================

void HostRelicIO::build(const PatternContext& context, uint8_t segmentId)
{
    const size_t count = context.fixtureCount;

    strip_segments.clear();
    strips.clear();
    nodes.clear();

    strip.reset(new eio::HSVStrip(static_cast<uint16_t>(count), 0));

    auto segment = std::make_unique<eio::HSVStripSegment>(strip.get(), segmentId);

    for (size_t idx = 0; idx < count; ++idx)
    {
        // The same node GeneratorPattern builds - spaced when the rig said
        // what each node is part of - so a scanner look asking which object
        // it is lighting gets the same answer in a machine as bare.
        auto node = edmx::makeStripNode(context, segment.get(), idx);

        if (idx < context.nodeCoords.size())
        {
            // The rig knows where its nodes are. Most scanner looks read the
            // strip index and never notice, but a look that reads height
            // (the record countdown's bottom-to-top sweep) gets the
            // obelisk's real geometry instead of a straight run.
            node->coord = context.nodeCoords[idx];
        }
        else
        {
            const float position = (idx < context.positions.size()) ? context.positions[idx] : 0.0f;
            node->coord = context.coords.at(position);
        }

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
    io->build(context, segmentId);

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
        //
        // A def that carries makeState is the exception: its wrapper does
        // something the machine needs (the scanner's rewinds the look's clock
        // on entry) and depends on no hardware, so it runs here as itself.
        std::shared_ptr<State_GenericHSV> state;
        if (def.makeState)
        {
            state = def.makeState(def.name.c_str(), io.get(), generator);
        }
        else
        {
            state = std::make_shared<State_GenericHSV>(def.name.c_str(), io.get());
            state->setGenerator(generator);
        }
        state->init();

        manager->addState(state);
        instances.push_back(state);
        generators.push_back(generator);
        generator->setUnderlay(underlay);
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

void StateMachinePattern::setUnderlay(const eanim::Underlay* inUnderlay)
{
    underlay = inUnderlay;
    for (const std::shared_ptr<eanim::GeneratorHSV>& generator : generators)
    {
        if (generator)
        {
            generator->setUnderlay(underlay);
        }
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

void StateMachinePattern::reflectCurves(eanim::CurveBag& bag)
{
    // Same shape and same lifetime rules as reflect(): the showing look's
    // drawable curves, and only its.
    if (activeIndex < generators.size() && generators[activeIndex])
    {
        generators[activeIndex]->reflectCurves(bag);
    }
}

bool StateMachinePattern::trigger(const ecore::GameplayTag& tag)
{
    // The showing look's, like reflect(): a ping lands on what is lit. The
    // outgoing look of a cross-fade is not told - it is on its way out.
    if (activeIndex < generators.size() && generators[activeIndex])
    {
        return generators[activeIndex]->onTrigger(tag);
    }
    return false;
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
        // Not a no-op: naming the showing state again restarts it. The game
        // rewinds a look by transitioning to it. (State 0, what the machine
        // shows from process launch, used to be power_up - so a boot's first
        // `state power_up` landed on a look already played out, and this
        // restart was what made it visible. The scanner's state 0 is `none`
        // now, dark, and the first power_up is a real change.)
        if (machine && target < instances.size())
        {
            machine->restartState(instances[target]);
        }
        return true;
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

void StateMachinePattern::setTransitionTime(float seconds)
{
    transitionTime = std::max(seconds, 0.0f);
    if (machine)
    {
        machine->transitionTime = transitionTime;
    }
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

// ============================================================================
// The scanner
// ============================================================================

namespace
{
    /// The part every scanner look shares: the name, and the State_ScannerHSV
    /// wrapper that rewinds the look's clock on entry so a timed look like
    /// power_up plays from its start each visit. `make` is the caller's to
    /// fill in - kept out of here so a pattern with no default constructor
    /// never has one instantiated for it.
    StateDef scannerDef(const char* name)
    {
        StateDef def;
        def.name = name;
        def.makeState = [](const char* stateName, eio::RelicIO* io,
                           std::shared_ptr<eanim::GeneratorHSV> generator) {
            return std::static_pointer_cast<State_GenericHSV>(
                std::make_shared<scanner::State_ScannerHSV>(stateName, io,
                    std::static_pointer_cast<scanner::PatternScanner>(generator)));
        };
        return def;
    }

    /// A scanner look built with its defaults.
    template <typename PatternT>
    StateDef scannerLook(const char* name)
    {
        StateDef def = scannerDef(name);
        def.make = []() {
            return std::static_pointer_cast<eanim::GeneratorHSV>(std::make_shared<PatternT>());
        };
        return def;
    }

    /// A scanner look that is one flat colour.
    StateDef scannerSolid(const char* name, const ecore::HSV& color)
    {
        StateDef def = scannerDef(name);
        def.make = [color]() {
            return std::static_pointer_cast<eanim::GeneratorHSV>(
                std::make_shared<scanner::Pattern_Scanner_Solid>(color));
        };
        return def;
    }

    /// A scanner look whose pattern takes constructor arguments - the pulse
    /// trio share one class and differ only in their numbers.
    template <typename PatternT, typename... Args>
    StateDef scannerLookWith(const char* name, Args... args)
    {
        StateDef def = scannerDef(name);
        def.make = [args...]() {
            return std::static_pointer_cast<eanim::GeneratorHSV>(
                std::make_shared<PatternT>(args...));
        };
        return def;
    }

    /// A look that is a plain GeneratorHSV, not a PatternScanner - no clock
    /// to rewind, so the default State_GenericHSV wrapper is the right one.
    /// The obelisk's ambient looks are these.
    /// The same def, with the look leaving the obelisk to the underlay -
    /// for the states that are the ring's alone. The make is wrapped rather
    /// than the class changed because `none` is a Solid the success and
    /// failure states also are, and those do paint the tower.
    StateDef overUnderlay(StateDef def)
    {
        auto inner = def.make;
        def.make = [inner]() {
            std::shared_ptr<eanim::GeneratorHSV> look = inner();
            // scannerLook / scannerSolid only ever build PatternScanners
            static_cast<scanner::PatternScanner*>(look.get())->leaveObeliskToUnderlay = true;
            return look;
        };
        return def;
    }

    template <typename PatternT>
    StateDef plainLook(const char* name)
    {
        StateDef def;
        def.name = name;
        def.make = []() {
            return std::static_pointer_cast<eanim::GeneratorHSV>(std::make_shared<PatternT>());
        };
        return def;
    }
}

std::unique_ptr<StateMachinePattern> edmx::makeScannerStateMachine()
{
    using namespace scanner;
    using ecore::HSV;

    // The same table ObeliskCore registers, and for the same reason: these
    // names are afterglow's own state tags, so the python game can send its
    // transitions verbatim. Variants the game picks with save-file flags
    // (emergency lock, broken device, discovery, seed verdict) are their own
    // tags - only the game knows the save, it just names the look it wants.
    std::vector<StateDef> states = {
        // State 0 is what the machine shows from process launch until the
        // game's first `state` line, and it is nothing: dark. It used to be
        // power_up, so the boot look played once on its own while the game
        // was still loading and again when the game asked for it.
        // over an underlay (the obelisk's own look, shadowed on the desk)
        // these three leave the tower alone: the boot is the ring's, and
        // the dark of `none` is the ring's - a take or a cross-fade through
        // them then changes nothing on the sculpture
        overUnderlay(scannerSolid("none", HSV(0.0f, 0.0f, 0.0f))),
        overUnderlay(scannerLook<Pattern_Scanner_PowerUp>("power_up")),
        overUnderlay(scannerLook<Pattern_Scanner_Boot>("boot")),
        scannerLook<Pattern_Scanner_ScanIdle>("scan_idle"),
        scannerLook<Pattern_Scanner_Emergency>("scan_idle_emergency"),
        scannerSolid("scan_idle_broken", HSV(0.0f, 0.0f, 0.0f)),
        scannerLook<Pattern_Scanner_DetectedWave>("scan_item_detected_filter"),
        scannerLook<Pattern_Scanner_DetectedShimmer>("scan_item_detected_generic"),
        scannerLook<Pattern_Scanner_DetectedShimmer>("scan_item_detected_seed"),
        scannerLook<Pattern_Scanner_DetectedMushroom>("scan_item_detected_mushroom"),
        scannerLook<Pattern_Scanner_DetectedMushroomNew>("scan_item_detected_mushroom_new"),
        scannerSolid("scan_item_success", HSV(120.0f, 1.0f, 0.392f)),   // rgb(0,100,0)
        scannerLook<Pattern_Scanner_SuccessMushroom>("scan_item_success_mushroom"),
        scannerSolid("scan_item_success_secret", HSV(120.0f, 1.0f, 0.392f)),
        scannerSolid("scan_item_failure", HSV(0.0f, 1.0f, 0.471f)),     // rgb(120,0,0)
        scannerLook<Pattern_Scanner_PlaybackMushroom>("audio_playback_mushroom"),
        scannerLook<Pattern_Scanner_PlaybackGeneric>("audio_playback_generic"),
        scannerSolid("audio_playback_seed", HSV(120.0f, 1.0f, 0.392f)),
        scannerSolid("audio_playback_seed_bad", HSV(0.0f, 1.0f, 0.392f)), // rgb(100,0,0)
        scannerSolid("audio_playback_rest", HSV(0.0f, 0.0f, 0.0f)),

        // The recording flow and the void stone - the last looks the game
        // rendered in python, so every state the scanner has is now here.
        // waiting for the rock: the amber breath, whole at the foot of the
        // tower and fading out to the sculpture's own picture at the tip
        scannerLookWith<Pattern_Scanner_SinePulse>("record_arm",
            HSV(45.0f, 1.0f, 1.0f), 4.0f, 0.15f, 0.5f, 1.0f),           // CRGB(1.0, 0.75, 0.0)
        scannerLook<Pattern_Scanner_RecordCountdown>("record_countdown"),
        scannerLook<Pattern_Scanner_RecordComet>("record_active"),
        scannerLookWith<Pattern_Scanner_SinePulse>("record_saved",
            HSV(132.0f, 1.0f, 1.0f), 6.0f, 0.4f, 0.6f),                 // CRGB(0.0, 1.0, 0.2)
        // One recording look, not two: detected_recording flows straight into
        // playback_recording (game.py's buildup-then-echo), and giving each
        // its own amber pattern meant a visible seam between two states that
        // are one moment to a visitor. Both are the amber wave now.
        scannerLookWith<Pattern_Scanner_DetectedWave>("scan_item_detected_recording",
            HSV(33.3f, 0.9f, 1.0f)),                                    // CRGB(1.0, 0.6, 0.1)
        scannerLookWith<Pattern_Scanner_DetectedWave>("audio_playback_recording",
            HSV(33.3f, 0.9f, 1.0f)),
        scannerLookWith<Pattern_Scanner_SinePulse>("void",
            HSV(282.0f, 1.0f, 0.5f), 2.0f, 0.0f, 0.6f),                 // CRGB(0.35, 0.0, 0.5)

        // The cleanse: a rock shown to an armed cleanse loses its take. The
        // fire burns over the tower's own picture while it waits, doused on
        // the game's cue as the rock lands (scanner.cleanse.douse); done is
        // a cool breath, the rock void again.
        scannerLook<Pattern_Scanner_CleanseFire>("cleanse_arm"),
        scannerLookWith<Pattern_Scanner_SinePulse>("cleanse_done",
            HSV(200.0f, 0.6f, 1.0f), 6.0f, 0.4f, 0.6f),
    };

    // The looks read the strip index, not coordinates, so the frame is the
    // default straight run. Segment id 0: nothing branches on it.
    CoordFrame frame;

    // 0.5s covers most of the game's transitionTo times; the game overrides
    // per change with `state <tag> <seconds>`.
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "scanner", std::move(states), 0, frame, 0.5f));
}

std::unique_ptr<StateMachinePattern> edmx::makeGenericStateMachine()
{
    using namespace scanner;

    // The generic looks as one machine, like mythos26: each look a state,
    // with the machine's real cross-fade between them - so the desk shows
    // one cue list instead of eleven top-level patterns, and switching looks
    // blends instead of cutting. scannerLook's wrapper rewinds a look's
    // clock on entry, which is also what clears the particle pools.
    std::vector<StateDef> states = {
        scannerLook<Pattern_Generic_MatrixRain>("matrix_rain"),
        scannerLook<Pattern_Generic_Fire2012>("fire_2012"),
        scannerLook<Pattern_Generic_Flow>("flow"),
        scannerLook<Pattern_Generic_Lake>("lake"),
        scannerLook<Pattern_Generic_Pacifica>("pacifica"),
        scannerLook<Pattern_Generic_Phased>("phased"),
        scannerLook<Pattern_Generic_Saw>("saw"),
        scannerLook<Pattern_Generic_SpotsFade>("spots_fade"),
        scannerLook<Pattern_Generic_TwinkleUp>("twinkleup"),
        scannerLook<Pattern_Generic_Waterfall>("waterfall"),
        scannerLook<Pattern_Generic_ColorClouds>("color_clouds"),
        scannerLook<Pattern_Generic_Rainbow>("rainbow"),
        scannerLook<Pattern_Generic_Chase>("chase"),
        // borrowed: the obelisk's theater and blobs looks play fine anywhere
        // the stage's coordinates reach, so they sit in the generic list too
        plainLook<Pattern_Obelisk_Theater>("theater"),
        plainLook<Pattern_Obelisk_Blobs>("blobs"),
    };

    CoordFrame frame;
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "generic", std::move(states), 0, frame, 0.5f));
}

std::shared_ptr<eanim::GeneratorHSV> edmx::makeObeliskLook(const std::string& name)
{
    if (name == "seasons" || name == "main") return std::make_shared<Pattern_Obelisk_FourSeasons>();
    if (name == "theater")                   return std::make_shared<Pattern_Obelisk_Theater>();
    if (name == "mono" || name == "test")    return std::make_shared<Pattern_Obelisk_Monocolor>();
    if (name == "blobs")                     return std::make_shared<Pattern_Obelisk_Blobs>();
    return nullptr;
}

std::unique_ptr<StateMachinePattern> edmx::makeObeliskStateMachine()
{
    // The obelisk's own ambient looks, as one machine. The default frame is
    // already the space they were tuned in - 8 x 43 from the origin - which
    // is what a normalized rig gets stretched across.
    std::vector<StateDef> states = {
        plainLook<Pattern_Obelisk_FourSeasons>("seasons"),
        // the seasons field generalised: one field, two pickable colours
        plainLook<Pattern_Obelisk_Blobs>("blobs"),
        plainLook<Pattern_Obelisk_Monocolor>("mono"),
    };

    CoordFrame frame;
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "obelisk", std::move(states), 0, frame, 0.5f));
}
