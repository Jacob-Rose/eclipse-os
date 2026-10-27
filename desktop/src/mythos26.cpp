// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edesk/mythos26.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>
#include <ebeat/beat_trigger.h>
#include <ebeat/level_bus.h>


using namespace edesk;

namespace
{
    /// One look in the table. Same shape as the relic version in
    /// state_machine.cpp: default-construct, init, hand back as a generator.
    ///
    /// Anything a look needs configuring is set on the instance afterwards
    /// rather than passed to a constructor — see `levelLook`. Keeping every
    /// entry built identically is what lets the table below read as a list.
    ///
    /// Both of these spell out the return type and return the derived pointer
    /// directly, rather than ending on a static_pointer_cast the way the relic
    /// table does. The cast leaves a derived-typed temporary to destroy, and
    /// gcc 16 devirtualises that destructor across the two generator types in
    /// this file, then warns that one of them overruns the other's allocation.
    /// It is a false positive — the vtable picks the right one at runtime — but
    /// letting the conversion happen at the return is both cleaner and quiet.
    template <typename PatternT>
    StateDef look(const char* name)
    {
        StateDef def;
        def.name = name;
        def.make = []() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<PatternT>();
            pattern->init();
            return pattern;
        };
        return def;
    }

    /// A look that wants to know it has been entered.
    ///
    /// The same shape as the scanner's State_ScannerHSV and for the same
    /// reason: entering is not something a generator can see. A beat look uses
    /// it to ask its trigger for a hit, so a cue comes up lit instead of dark
    /// for up to a bar - and asks the *shared* trigger, so everything on that
    /// rate comes up with it.
    template <typename PatternT>
    class State_BeatHSV : public State_GenericHSV
    {
    public:
        State_BeatHSV(const char* stateName, eio::RelicIO* io,
                      std::shared_ptr<PatternT> inPattern)
            : State_GenericHSV(stateName, io)
            , beatPattern(inPattern)
        {
            setGenerator(inPattern);
        }

    protected:
        virtual void onStateChangeState(StateStatus inStatus) override
        {
            // Entered fresh: either the machine is blending toward us, or we
            // were set active directly from Off. Becoming Active at the end of
            // a blend also lands here as Active, but from TransitionIn - asking
            // for a second hit then would fire twice on one cue change.
            const bool bEntering = inStatus == StateStatus::TransitionIn
                || (inStatus == StateStatus::Active && GetStatus() == StateStatus::Off);

            State_GenericHSV::onStateChangeState(inStatus);

            if (bEntering && beatPattern)
            {
                beatPattern->onEnter();
            }
        }

        std::shared_ptr<PatternT> beatPattern;
    };

    /// A look that fires on the beat, with the shape of its hit and how often.
    ///
    /// Attack and decay are what a beat look *is* - a crack and a trail, or a
    /// swell and a long fall - so they belong in the cue list beside the name
    /// rather than buried in a constructor, and the rate is beside them because
    /// a look that opens in half time is a different cue, not a mistuned one.
    /// All three stay live knobs once it is running; these are what it opens on.
    ///
    /// The lambda is where the concrete type is known, which is what keeps the
    /// machine itself from needing to know about any of them.
    template <typename PatternT>
    StateDef beatLook(const char* name, float attackSeconds, float decaySeconds, float pulseRate,
                      bool bHitOnEntry = true)
    {
        StateDef def;
        def.name = name;
        def.make = [attackSeconds, decaySeconds, pulseRate, bHitOnEntry]()
            -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<PatternT>();
            pattern->setEnvelope(attackSeconds, decaySeconds);
            pattern->setPulseRate(pulseRate);
            pattern->setHitOnEntry(bHitOnEntry);
            pattern->init();
            return pattern;
        };
        def.makeState = [](const char* stateName, eio::RelicIO* io,
                           std::shared_ptr<eanim::GeneratorHSV> generator) {
            return std::static_pointer_cast<State_GenericHSV>(
                std::make_shared<State_BeatHSV<PatternT>>(
                    stateName, io, std::static_pointer_cast<PatternT>(generator)));
        };
        return def;
    }

    /// A light held where it is put, in whatever colour it is given.
    ///
    /// The UV par's `off` and `on` are this at 0 and 1. `level` is a knob so
    /// `on` can be trimmed at the desk without becoming a different state -
    /// and so a mod can turn it, which is what makes the same class the
    /// additive hit layers of config/audio_layers_test.json.
    StateDef levelLook(const char* name, float level)
    {
        StateDef def;
        def.name = name;
        def.make = [level]() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<Pattern_Mythos_Solid>();
            pattern->level = level;
            return pattern;
        };
        return def;
    }

    /// A show look on the stage: a scanner::PatternScanner, wrapped so its
    /// clock is wound back on entry - the same State_ScannerHSV the generic
    /// machine uses, and for the same reason: the rain and the fire start
    /// empty on every visit rather than mid-storm.
    ///
    /// `setup` is run on the fresh look, and is where a cue states its
    /// colours or its channel: two cues on one class differ only in what it
    /// is handed, and the difference reads as a line each. What it leaves is
    /// mode 1, and `modes` are 2 and 3 on top of it - each a diff on the cue
    /// as set up, never on the other; see ShowModes. A cue with none has a
    /// mode pad that puts it back where it was.
    template <typename PatternT>
    using LookSetup = std::function<void(PatternT&)>;

    template <typename PatternT>
    StateDef showLook(const char* name, LookSetup<PatternT> setup = {},
                      std::vector<LookSetup<PatternT>> modes = {})
    {
        StateDef def;
        def.name = name;
        def.make = [setup, modes]() -> std::shared_ptr<eanim::GeneratorHSV> {
            auto pattern = std::make_shared<PatternT>();
            if (setup)
            {
                setup(*pattern);
            }
            // The variants close over the look itself, which holds them: a
            // look is never copied out from under its shared_ptr, so the
            // pointer is good for as long as the closure is.
            std::vector<std::function<void()>> variants;
            for (const LookSetup<PatternT>& variant : modes)
            {
                PatternT* raw = pattern.get();
                variants.push_back([raw, variant] { if (variant) variant(*raw); });
            }
            pattern->initModes(*pattern, std::move(variants));
            return pattern;
        };
        def.makeState = [](const char* stateName, eio::RelicIO* io,
                           std::shared_ptr<eanim::GeneratorHSV> generator) {
            return std::static_pointer_cast<State_GenericHSV>(
                std::make_shared<scanner::State_ScannerHSV>(stateName, io,
                    std::static_pointer_cast<scanner::PatternScanner>(generator)));
        };
        return def;
    }
}

StateDef edesk::beatPulseState()
{
    // The same numbers the show's cue opened on - a 0.15s rise into a 0.60s
    // fall, on the beat - because the look is unchanged; only the list holding
    // it is. See the note on beatLook for what the three of them are.
    return beatLook<Pattern_Mythos_BeatPulse>("beat_pulse", 0.15f, 0.60f, 1.0f);
}

std::unique_ptr<StateMachinePattern> edesk::makeUvStateMachine()
{
    // The UV par's three modes, as a layer's machine. `flash` is the show's
    // beat pulse - the same envelope, drawable at the desk, the same rate
    // knob - on a light that is nothing but a level, so the UV hits on the
    // beat while the truss around it follows the scanner.
    //
    // It does not hit on entry, which is the one place its cue list differs
    // from the show's. A cue coming up wants to be seen immediately; the UV is
    // joining a grid the rig is already running, and a hit at the instant the
    // operator pressed the button is a flash off the beat - which is precisely
    // what made it look out of step with the truss. It waits, and lands with
    // everything else on the `beat` trigger.
    //
    // `kick` and `rainbow` are the show's asks: the blacklight on every kick,
    // a pad away under any cue, and breathing through a hue wheel -
    // which on three banks of one colour is a third to two thirds and never
    // off - under the rain. Both are looks the show has anyway, pointed at
    // one light.
    //
    // The kick is a glow, not a strobe: it rests at a low level, lands at
    // most of full on the kick and falls away over a third of a second. It
    // was the bass hits raw - black to full and back on every transient -
    // and on a par beside the truss that was the flashing blown and the
    // punk were both too much of.
    std::vector<StateDef> states = {
        levelLook("off", 0.0f),
        //                                     attack decay  rate  entry hit
        beatLook<Pattern_Mythos_BeatPulse>("flash", 0.02f, 0.30f, 1.0f, false),
        levelLook("on", 1.0f),
        showLook<Pattern_Mythos_BusWash>("kick", [](Pattern_Mythos_BusWash& look) {
            look.color = ecore::HSV(0.0f, 0.0f, 1.0f);
            look.channel = AudioChannel::BassHits;
            look.gain = 0.0f;
            look.floorLevel = 0.15f;
            look.hitColor = ecore::HSV(0.0f, 0.0f, 1.0f);
            look.hit = 0.75f;
            look.hitDecay = 0.35f;
        }),
        look<Pattern_Mythos_HueCycle>("rainbow"),
    };

    CoordFrame frame;
    // A second, like the show's cues: flash into on, or on into the kick
    // wash, is a mode the eye follows across, and at 0.15s it was a cut.
    // A cue that wants the snap says `layer uv state on 0` or `cut`.
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "uv", std::move(states), 0, frame, 1.0f));
}

std::unique_ptr<StateMachinePattern> edesk::makeFlashStateMachine()
{
    // The bpm flash, as a layer over the whole rig. The same beat pulse the
    // UV runs, white unless its `color` knob says otherwise, and off until a
    // cue asks - at `blend: add` a layer at black is not there, so the cues
    // that do not want it never know it is patched.
    //
    // No `on`: a solid white summed over every fixture is a whiteout, not a
    // mode anyone would pick from a pad.
    std::vector<StateDef> states = {
        levelLook("off", 0.0f),
        //                                     attack decay  rate  entry hit
        beatLook<Pattern_Mythos_BeatPulse>("flash", 0.02f, 0.30f, 1.0f, false),
    };

    CoordFrame frame;
    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "flash", std::move(states), 0, frame, 0.15f));
}

std::unique_ptr<StateMachinePattern> edesk::makeMythos26StateMachine()
{
    // ------------------------------------------------------------------
    // The show, one line per cue, in the order a UI shows them - and the
    // order of "pattern spec.txt" at the top of the checkout, which is where
    // the numbers on the surface come from. Sixteen cues: fourteen looks
    // and the two house states either side of the set.
    //
    // What a cue asks of the *rest* of the room - its Synesthesia scene and
    // media, the flash layer, the UV - is not here. A look is a look; the
    // cue table in config/mythos-show.json says what goes with it, and the
    // surface fires the lot from one pad.
    //
    // Writing one means adding a GeneratorHSV to mythos26.h/.cpp and changing
    // its line here; nothing else in the runner, the protocol or the UI needs
    // to know.
    //
    // Renaming a state means renaming it in three places: here,
    // MYTHOS26_STATES in python/eclipse_dmx/config.py, and the button table in
    // python/eclipse_dmx/viewer.py. The executable is the authority; the other
    // two are so a config can be validated and a button can be labelled
    // without one running.
    // ------------------------------------------------------------------
    using ecore::HSV;

    // Each cue is its setup, then its modes 2 and 3 - what the pad steps
    // through, each a diff on the cue as set up. A cue with no modes listed
    // still has the pad; it puts the cue back where it was.
    using NoiseWash = Pattern_Mythos_NoiseWash;
    using BusWash = Pattern_Mythos_BusWash;
    using KickColor = Pattern_Mythos_KickColor;
    using Rain = Pattern_Mythos_Rain;
    using Fire = Pattern_Mythos_Fire;
    using Strobe = Pattern_Mythos_GradientStrobe;
    using Canyon = Pattern_Mythos_CanyonWave;
    using Nova = Pattern_Mythos_Nova;
    using Clouds = Pattern_Mythos_CloudFlight;
    using Reaction = Pattern_Mythos_Reaction;
    using Scaffold = Pattern_Mythos_Scaffold;
    using House = Pattern_Mythos_House;

    std::vector<StateDef> states = {
        // 1. cyan into deep blue, drifting - the neuron scene's own colours:
        //    its particles lerp vec3(0,1,.8) to vec3(0,.3,1), hue 168 to 222.
        //    2 is the same field slowed and widened; 3 quick and busy.
        showLook<NoiseWash>("neuron", [](NoiseWash& look) {
            look.colorA = HSV(170.0f, 0.90f, 1.00f);
            look.colorB = HSV(222.0f, 1.00f, 0.40f);
        }, {
            [](NoiseWash& look) { look.speed = 0.35f; look.scale = 1.6f; },
            [](NoiseWash& look) { look.speed = 2.5f;  look.scale = 0.7f; },
        }),
        // 2. red, riding whichever level is there - Mixxx's instant VU
        //    (the meter that peaks on every kick, off in the mapping until
        //    someone turns it on), Mixxx's average, or Synesthesia's bass -
        //    and swelling to a hotter red on Synesthesia's bass hits. On
        //    the instant alone it sat at its floor whenever the cable was
        //    not sending it, which was most nights. Never below a fifth,
        //    and no white flash over it: the swell is the beat. The geode
        //    shifts to blue, so 2 is blue: a grained blue field on the same
        //    levels with a paler blue landing on the kick. 3 keeps the red
        //    and lands blue on the kick instead.
        showLook<BusWash>("geode", [](BusWash& look) {
            look.color = HSV(0.0f, 1.0f, 1.0f);
            look.channel = AudioChannel::LevelInstant;
            look.alsoChannels = {AudioChannel::LevelAverage, AudioChannel::Bass};
            look.floorLevel = 0.2f;
            look.slew = 0.1f;
            look.hitColor = HSV(14.0f, 1.0f, 1.0f);
            look.hit = 0.8f;
            look.hitDecay = 0.3f;
        }, {
            [](BusWash& look) {
                look.color = HSV(228.0f, 1.0f, 1.0f);
                look.floorLevel = 0.3f;
                look.texture = 0.7f;
                look.hitColor = HSV(200.0f, 0.55f, 1.0f);
                look.hit = 1.0f;
                look.hitDecay = 0.3f;
            },
            [](BusWash& look) {
                look.hitColor = HSV(225.0f, 1.0f, 1.0f);
                look.hit = 1.0f;
                look.hitDecay = 0.35f;
            },
        }),
        // 3. the rain, every drop its own colour, and no churn on the tails:
        //    smooth streaks rather than the film's glyph flicker. 2 is the
        //    same rain with the video up (the cue table's half); 3 is the
        //    film's green, a downpour, the colour gliding to it over a
        //    second rather than cutting; 4 is the same downpour gone white.
        //    The pool is 96, so 80 is a real number.
        showLook<Rain>("rain", [](Rain& look) {
            look.flicker = 0.0f;
        }, {
            {},
            [](Rain& look) { look.targetHueSpread = 0.0f; look.dropCount = 80.0f; look.fallSpeed = 18.0f; },
            [](Rain& look) { look.targetHueSpread = 0.0f; look.targetWhite = 1.0f;
                             look.dropCount = 80.0f; look.fallSpeed = 18.0f; },
        }),
        // 4. fire. The pars read the obelisk's bottom run - the ember bed
        //    and the flames as they are born, not the fifth run where they
        //    pass by whole - averaged over a patch, so each par is the
        //    colour of its stretch of fire moving rather than a flicker.
        //    2 is embers - a few risers, dying young; 3 is the whole bed
        //    alight and climbing fast.
        showLook<Fire>("fire", [](Fire& look) {
            look.trussRow = scanner::kStageBottom;
            look.trussBlur = 1.5f;
        }, {
            [](Fire& look) { look.sparking = 0.15f; look.cooling = 60.0f; },
            [](Fire& look) { look.sparking = 0.85f; look.cooling = 20.0f; look.riseSpeed = 18.0f; },
        }),
        // 5. a new colour on every beat. The Glitch scene re-deals its
        //    background on syn_OnBeat, which the audio bus carries as `beat`
        //    - so the rig re-deals on the same signal, not a bass hit near
        //    it, and on the same line: the scene's is 0.9, and the rig's
        //    0.45 caught the detector's half-beats the scene ignores. 0.8
        //    rather than 0.9 because the bus samples the spike at 30Hz and
        //    can land after its peak. No faster than three quarters of a
        //    beat at the clock's tempo, for the double-fires, and only with
        //    music playing - Mixxx's meters or Synesthesia's presence up -
        //    because the detector carries on beating to silence. A high
        //    floor, so a re-deal is the colour changing more than the rig
        //    flashing - but torn: the stage in five bands, half of them
        //    dealt the complement, the way the scene re-deals in slices.
        //    The pars sit lower and take each deal as a ripple up the
        //    truss; at the rig's floor they barely moved. 2 is one colour
        //    across the rig; 3 darker between hits and torn further apart
        //    on each.
        showLook<KickColor>("glitch", [](KickColor& look) {
            look.channel = AudioChannel::Beat;
            look.threshold = 0.8f;
            look.holdoff = 0.3f;
            look.beatHoldoff = 0.75f;
            look.gateChannels = {AudioChannel::LevelAverage, AudioChannel::LevelInstant,
                                 AudioChannel::Presence};
            look.gate = 0.1f;
            look.floorLevel = 0.65f;
            look.decay = 0.45f;
            look.slices = 5.0f;
            look.split = 0.5f;
            look.trussFloor = 0.3f;
            look.trussSweep = 0.15f;
        }, {
            [](KickColor& look) { look.scatter = 0.0f; look.split = 0.0f; },
            [](KickColor& look) {
                look.floorLevel = 0.2f; look.scatter = 0.3f; look.decay = 0.2f;
                look.trussFloor = 0.1f;
            },
        }),
        // 6. pink into purple, drifting - the fire tunnel behind it - and
        //    breathing with Mixxx's average VU, the cable's presence, so it
        //    moves with the track like the cues either side of it. Slewed
        //    longer than the geode: that meter is the level against its
        //    two-second window, so it still rises on a kick, and a wash
        //    should breathe with the track rather than pump. No flash by
        //    default; the pad is there. The same 2 and 3 as the neuron.
        showLook<NoiseWash>("tunnel", [](NoiseWash& look) {
            look.colorA = HSV(325.0f, 0.85f, 1.00f);
            look.colorB = HSV(275.0f, 1.00f, 0.55f);
            look.channel = AudioChannel::LevelAverage;
            look.follow = 1.0f;
            look.floorLevel = 0.3f;
            look.slew = 0.5f;
        }, {
            [](NoiseWash& look) { look.speed = 0.35f; look.scale = 1.6f; },
            [](NoiseWash& look) { look.speed = 2.5f;  look.scale = 0.7f; },
        }),
        // 7. pink riding the mids, never below 0.65 - a pink base all the
        //    way through, rather than dark until it pops - and the kick a
        //    pop of pink with a green afterglow, which is the order Filter
        //    Blown v2's palette runs in: black through magenta (hue 321) to
        //    green (hue 119) at full motion. The kick at a little over half,
        //    falling slower, and a third of that on the pars - at full it
        //    was a strobe, and on the truss worst. The base drifts between
        //    hot pink and a paler rose, and the green lands in patches over
        //    a third of the rig - the accent, not the rig's second colour;
        //    elsewhere the kick is the pink pop alone. 2 lifts the base; 3
        //    is the old cue, dark until the pop.
        showLook<BusWash>("blown", [](BusWash& look) {
            look.color = HSV(321.0f, 0.95f, 1.0f);
            look.channel = AudioChannel::MidPresence;
            look.floorLevel = 0.65f;
            look.slew = 0.3f;
            look.hitColor = HSV(119.0f, 1.0f, 1.0f);
            look.hit = 0.6f;
            look.hitDecay = 0.6f;
            look.afterglow = 1.0f;
            look.trussHit = 0.35f;
            look.color2 = HSV(338.0f, 0.65f, 1.0f);
            look.blend = 0.8f;
            look.accent = 0.35f;
        }, {
            [](BusWash& look) { look.floorLevel = 0.85f; },
            [](BusWash& look) { look.floorLevel = 0.25f; },
        }),
        // 8. the rainbow when the track is there, white noise when it is
        //    not: the wheel across the stage turning slowly, blended in by
        //    the presence over a low white grain - over most of the meter,
        //    and slewed, so it swells rather than switches. 2 is the rainbow
        //    whatever the music; 3 quick - the wheel twice across the stage
        //    and turning fast.
        showLook<Reaction>("reaction", {}, {
            [](Reaction& look) { look.threshold = 0.0f; look.knee = 0.01f; },
            [](Reaction& look) { look.span = 2.0f; look.hueRate = 0.3f; },
        }),
        // 9. punk purple into honey orange, leaning toward a deep blue as
        //    far as Mixxx's average VU says. It used to drop to a navy at a
        //    fifth of full and back on the meter's every wobble, which was
        //    the rig flashing hard all the way through; now the blue is
        //    nearly as bright as the gradient, the lean stops a little
        //    short of it and is slewed, so the rig holds its level and the
        //    track moves its colour. The flash layer and the UV stay off;
        //    the UV's pad is there. The pars move as the scene's smoke
        //    does: swirled and lifted by the bass hits, flipped toward the
        //    other colour on the beat - colour on lamps that stay lit, not a
        //    strobe. 2 and 3 are the beat instead of the meter, in half time
        //    and in double, the same colour lean.
        showLook<Strobe>("punk", [](Strobe& look) {
            look.strobeColor = HSV(228.0f, 1.0f, 0.8f);
            look.depth = 0.6f;
            look.slew = 0.3f;
            look.trussFloor = 0.45f;
            look.swirl = 0.3f;
            look.trussInvert = 0.6f;
        }, {
            [](Strobe& look) { look.follow = 0.0f; look.setPulseRate(edesk::kHalfTime); },
            [](Strobe& look) { look.follow = 0.0f; look.setPulseRate(edesk::kDoubleTime); },
        }),
        // 10. the canyon fly-through - the bands eased into one another and
        //     half the loop on the rig at once, so it is a gradient and not
        //     stripes - and a rainbow on the beat. 2 is a slow fly with the
        //     whole loop on the rig and the rainbow once a bar; 3 fast, the
        //     rainbow in double.
        showLook<Canyon>("canyon", {}, {
            [](Canyon& look) { look.speed = 0.05f; look.waves = 1.0f; look.setPulseRate(edesk::kQuarterTime); },
            [](Canyon& look) { look.speed = 0.30f; look.setPulseRate(edesk::kDoubleTime); },
        }),
        // 11. the scaffold flying past: cyan and red-orange struts
        //     sweeping down the stage, one past any point per beat on the
        //     beat clock, over a dim blue fog that stays lit, the struts a
        //     little brighter on the beat. It was the whole pattern popping
        //     out of black on every hit, which was a flash. 2 is the
        //     scaffold lit - the fog up, wider struts, a slower sweep; 3 is
        //     hard - the fog gone, more struts and twice the speed.
        showLook<Scaffold>("scaffold", {}, {
            [](Scaffold& look) {
                look.ground = HSV(205.0f, 0.90f, 0.22f);
                look.strutWidth = 0.45f;
                look.strutRate = 0.5f;
            },
            [](Scaffold& look) {
                look.ground = HSV(205.0f, 0.90f, 0.0f);
                look.struts = 5.0f;
                look.strutRate = 2.0f;
                look.lift = 0.45f;
            },
        }),
        // 12. the churn: Eclipse Churn is Churning painted in two rig
        //     colours, so this is the field in the same two, busier than
        //     the neuron and pushed by the level the way the paint is - and
        //     with the field's contrast up, so a patch is red or blue far
        //     more often than the purple between them: the scene's paint
        //     is hard-edged and the rig was mostly the mix. The level
        //     speeds the field up as it pushes the scene's paint, and the
        //     bass hits punch it toward full and let it fall - on the pars
        //     most of that, and their contrast eased, so a lamp leans
        //     between the colours rather than flipping on every pass. Four
        //     modes, a pad each: three palettes and a rainbow. 1 is the
        //     scene's own pair, red and blue; 2 magenta and cyan; 3 orange
        //     and violet; 4 the red-and-blue pair turned through the wheel,
        //     with the scene let back to its own rainbow (the cue table's
        //     half). The probe sends colour A; the cue table sends colour B.
        showLook<NoiseWash>("churn", [](NoiseWash& look) {
            look.colorA = HSV(0.0f, 0.95f, 1.00f);
            look.colorB = HSV(237.0f, 0.95f, 1.00f);
            look.channel = AudioChannel::Level;
            look.follow = 0.6f;
            look.floorLevel = 0.35f;
            look.speed = 1.6f;
            look.scale = 0.8f;
            look.contrast = 0.7f;
            look.push = 1.5f;
            look.kick = 0.45f;
            look.kickAttack = 0.02f;
            look.trussKick = 0.7f;
            look.trussContrast = 0.5f;
        }, {
            [](NoiseWash& look) { look.colorA = HSV(300.0f, 0.90f, 1.00f); look.colorB = HSV(185.0f, 0.95f, 0.90f); },
            [](NoiseWash& look) { look.colorA = HSV(28.0f, 0.95f, 1.00f);  look.colorB = HSV(268.0f, 0.90f, 0.80f); },
            [](NoiseWash& look) { look.hueCycle = 0.08f; },
        }),
        // 13. the nova: galaxies on a wash over a dark ground, swelling
        //     with the bass presence the way the scene's do. 1 is the
        //     palette chosen by hand - cyan galaxies, a yellow wash, and the
        //     ground the wash dimmed, which is the only ground the scene can
        //     show: its background is rig_color_2 * base_amount, so the cue
        //     table's 0.35 is this base's value. 2 is Nova's own regime 1,
        //     orange on sky blue over deep blue; 3 its regime 2, violet with
        //     an azure wash on near-black; 4 the rainbow: the scene derives
        //     its own second colour again - the complement of what the probe
        //     sends - so the galaxies go to the blue whose complement is the
        //     palette's yellow, and the three turn through the wheel with the
        //     wash staying opposite the galaxies the way the scene's does.
        //     The probe sends the galaxy colour; the cue table sends the wash.
        //     Like the scene, the field turns faster on the bass and a ring
        //     runs out from the middle of the stage on every beat - wider
        //     and eased in on the pars, so a lamp swells and lets go - and
        //     the wash pulses on the beat as far as the presence says.
        showLook<Nova>("nova", [](Nova& look) {
            look.push = 2.0f;
            look.pulse = 0.75f;
            look.trussRing = 0.22f;
            look.trussEase = 0.25f;
            look.ease = 0.25f;
            look.washPulse = 0.8f;
        }, {
            [](Nova& look) {
                look.galaxy = HSV(37.0f, 0.80f, 1.00f);
                look.wash = HSV(207.0f, 0.55f, 1.00f);
                look.base = HSV(240.0f, 0.75f, 0.30f);
            },
            [](Nova& look) {
                look.galaxy = HSV(263.0f, 0.90f, 1.00f);
                look.wash = HSV(216.0f, 0.75f, 1.00f);
                look.base = HSV(250.0f, 0.80f, 0.08f);
                look.washAmount = 0.35f;
            },
            [](Nova& look) { look.galaxy = HSV(229.0f, 0.90f, 1.00f); look.hueCycle = 0.06f; },
        }),
        // 14. the clouds: a sunset down the stage, yellow overhead into
        //     pink at the horizon, and the deck below it in shadow,
        //     streaming past, with wisps of it in the sky. The modes are
        //     the heights: the cue opens low, the deck over most of the
        //     rig; 2 is cruising, the horizon at half; 3 is high, the deck
        //     fallen away to a strip. The look glides between them over
        //     its `glide`, and the cue table ramps the scene's own height
        //     with it. `speed` is the cue's two pads, and a mode leaves it
        //     where they put it. The pars are one light set by the
        //     height - dim down in the deck, bright sunset above it - with
        //     the deck's clouds passing over that light, shading and paling
        //     it, and a sunset running pink to yellow along the line of
        //     them - rather than ten lamps each reading its own sky.
        showLook<Clouds>("clouds", [](Clouds& look) {
            look.trussLow = 0.25f;
            look.trussHigh = 1.0f;
            look.trussClouds = 0.6f;
            look.trussSpread = 0.6f;
        }, {
            [](Clouds& look) { look.height = 0.50f; },
            [](Clouds& look) { look.height = 0.85f; },
        }),
        // 15. the house lights: white at half, for the room after the
        //     show. 16. the blackout, for before it - the cue table turns
        //     both layers off with it.
        showLook<House>("white", [](House& look) { look.level = 0.5f; }),
        showLook<House>("blackout", [](House& look) { look.level = 0.0f; }),
    };

    // ------------------------------------------------------------------
    // The stage's space - the same frame the generic machine runs in.
    //
    // The show borrows the fire and the rain from that list, and every look
    // written here stands on the same stage so a wave that pours down the
    // obelisk pours through the ring and past the truss in one motion. The
    // environment places the obelisk and the ring in literal stage
    // coordinates, so this frame only ever decides where a *normalized*
    // device - the truss - is stretched to: the default puts it up the
    // stage's height beside the pillar, which is what a top-down cue wants.
    // ------------------------------------------------------------------
    CoordFrame stage;

    // A second. This was 0.25s - seven frames - on the theory that a
    // beat-locked look wants to arrive promptly, and at that length a blend
    // is not something anyone can see. A cue that must land on the beat
    // says so: `state <name> 0` or `cut` on the line.
    const float transitionTime = 1.0f;

    return std::unique_ptr<StateMachinePattern>(new StateMachinePattern(
        "mythos26", std::move(states),
        /*segmentId*/ 0, // nothing here branches on segment; only relic looks do
        stage,
        transitionTime));
}
