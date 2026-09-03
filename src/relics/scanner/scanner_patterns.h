// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../../lib/ecore/hsv.h"
#include "../../lib/ecore/math.h"
#include "../../lib/eanim/automation_curve.h"
#include "../../lib/eanim/generator_hsv.h"
#include "../../lib/esm/state_generic.h"

using namespace ecore;
using namespace eanim;
using namespace esm;

/// The triggers the scanner's looks answer to - the tags afterglow sends
/// with `trigger <tag>` as it plays the sound each one stands for. Declared
/// here, once, so the look and anything that fires it agree on the spelling.
namespace scanner_tags
{
    /// the scan ping sounded: scan_idle flashes and sends the pulse up
    inline const GameplayTag Ping{"scanner.ping"};
}

///
/// The scanner's looks, ported from afterglow (main-py/game/game.py).
///
/// The scanner itself is the Raspberry Pi running the pygame build of
/// afterglow: it keeps its own ring, audio, NFC reads, and state machine.
/// These are the same looks in eclipse-os pattern language, so a relic on the
/// end of the scanner's USB cable can render a matching picture when the game
/// tells it which state it entered - see ObeliskCore::handleCommand("state ...").
///
/// Each pattern keeps its own clock rather than reading the state's, so the
/// same generator also runs bare on the desk through edmx::GeneratorPattern.
/// State_ScannerHSV winds that clock back to zero whenever its state is
/// entered, which is what the python states got for free by keying off
/// getStateTimeActive().
///

namespace scanner
{
    ///
    /// The stage: one 2d space shared by everything the game drives.
    ///
    /// x runs across and y runs up. The obelisk stands on the floor of it -
    /// eight runs at x 0..7, height y 0..42, exactly the coordinates
    /// ObeliskIO::init generates. The ring sits on the sculpture as a
    /// circle centred on the obelisk's width, the way the scanner is
    /// mounted on the tower, with its lowest pixel on the *origin line* -
    /// the height the DMX truss hangs at (desktop/config/scanner_stage.json),
    /// and where a look synced to the beat is to start from. The desktop
    /// rig (desktop/devices/scanner_ring.json) and the live game's inline
    /// config (main-py/lib/jr_lib/eclipse_engine.py) both write the ring at
    /// these coordinates; the constants here have to agree with them, and
    /// the desktop's tests check that they do.
    ///
    /// The point of sharing the space: a look that travels along y climbs
    /// the obelisk and passes through the ring on the way - one wave through
    /// one place, not the same look running twice from scratch.
    ///
    constexpr float kStageBottom = 0.0f;    ///< the floor: the obelisk's lowest run
    constexpr float kStageTop = 42.0f;      ///< the obelisk's highest
    /// the origin line: the truss, the ring's lowest pixel, and where a
    /// beat-synced look originates
    constexpr float kStageOriginY = 16.0f;
    constexpr float kRingRadius = 3.5f;
    constexpr float kRingCenterX = 3.5f;
    constexpr float kRingCenterY = kStageOriginY + kRingRadius;
    /// the obelisk's runs: integer x 0..7, a closed loop around four sides -
    /// column 7 is physically beside column 0
    constexpr int kStageColumns = 8;

    /* @brief A scanner look: a GeneratorHSV with its own resettable clock. */
    class PatternScanner : public GeneratorHSV
    {
    public:
        virtual void reset() { timeActive = 0.0f; }
        virtual void tick(float deltaTime) override { timeActive += deltaTime; }

        /// Every scanner look runs on this one clock; a look with more
        /// (particles, a pool) adds its own on top.
        virtual void reflectState(ecore::PropertyBag& bag) override { bag.addState("time", timeActive); }

        /// Set on the looks that have nothing to say about the obelisk - the
        /// boot animations, the dark `none` - so over an underlay the tower
        /// keeps its own picture through them instead of going black. See
        /// GeneratorHSV::leavesToUnderlay.
        bool leaveObeliskToUnderlay{false};

        virtual bool leavesToUnderlay(const HSVStripNode* node) const override
        {
            return leaveObeliskToUnderlay && eio::nodeSpace(node) == NodeSpace::Obelisk;
        }

    protected:
        /* @brief Where this node sits along its strip, 0..1.
        *
        * The python looks indexed a 35 pixel ring; anything that used
        * pixel / n becomes this, so the look stretches to whatever strip the
        * relic actually has.
        */
        static float stripAlpha(const HSVStripNode* node);

        /* @brief Where this node stands on the stage.
        *
        * A mapped node answers with its real coordinates. A bare strip
        * stands in as a vertical run - its index up the y axis - so the
        * spatial looks still move along it instead of rendering a flat wash.
        */
        static Coordinate nodeCoord(const HSVStripNode* node);

        /* @brief The node's height as a fraction of the stage, ring floor 0
        * to obelisk top 1. */
        static float stageAlpha(const HSVStripNode* node);

        float timeActive{0.0f};
    };


    /* @brief power_up: pixels wink on green one at a time in a scattered
    * order, later arrivals brighter, until the ring is filled.
    *
    * The python walked the ring with a stride of 9 and left each visited pixel
    * lit at the boot progress it was visited at. The stride is replaced by a
    * golden-ratio scatter so the fill order stays shuffled on any strip
    * length, including ones the stride would not have covered.
    */
    class Pattern_Scanner_PowerUp : public PatternScanner
    {
    public:
        float bootTime = 2.5f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief boot: a magenta swell that flickers with noise while it rises and
    * dies away.
    *
    * The two shapes are AutomationCurves on a normalized clock - time 1.0 is
    * the end of the boot, whatever bootTime says - which is the house
    * archetype for a drawn shape: the same evaluate the relics play, the same
    * key cap, and the desk's curve editor draws one and hands back the addKey
    * lines the constructor is made of. They replaced two equidistant float
    * tables from the python port; the noise table's sixteen stops compress
    * into keys with easing carrying the shoulders.
    */
    class Pattern_Scanner_Boot : public PatternScanner
    {
    public:
        Pattern_Scanner_Boot();

        float bootTime = 7.5f;

        /// scales the flicker curve: 0 is a clean swell, past 1 the noise
        /// eats further into it than the curve says. Independent of bootTime,
        /// which is the point - each is its own curve target.
        float noiseGain = 1.0f;

        /// both on the normalized 0..1 boot clock
        eanim::AutomationCurve brightnessCurve;
        eanim::AutomationCurve noiseCurve;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
        virtual void reflectCurves(eanim::CurveBag& bag) override;
    };


    /* @brief scan_idle: the ring breathes cyan on a 2 second heartbeat, each
    * flash sends a pulse up the obelisk from its foot to its tip, and the
    * truss stays dark (a scanner along it is there behind a knob).
    *
    * The first look to read the objects rather than the stage: each node
    * says which space it is in (eio::spaceOf), and the look renders each
    * object in its own terms - the obelisk by strip around its four sides,
    * the truss by its length - while a node with no space of its own (the
    * ring, a bare strip) gets the breath, which is what every node got
    * before. The breath is an AutomationCurve on the normalized cycle, like
    * the boot swell - the python table's run of trailing zeros collapses to
    * its two endpoint keys.
    *
    * One clock runs the ring and the sculpture. The ring plays the breath
    * curve; the obelisk plays the pulse curve, and a row's height delays its
    * read into it - the sweep record_countdown does - so the pulse leaves
    * the foot as the ring flashes and reaches the tip riseCycles later, every
    * side at once, one band climbing the tower. It used to be the breath
    * itself going round the sides a lap-share at a time, and before that a
    * beam on a rotation of its own that never agreed with the heartbeat.
    *
    * The clock is the game's when the game is there. Each time afterglow
    * plays the scan ping it sends `trigger scanner.ping`, and the look re-anchors
    * its cycle so the flash lands on the ping's transient - then holds at
    * the end of that cycle, dark, until the next one, so a ping longer than
    * cycleTime never gets a second, unheard flash. Before the first ping
    * (a desk viewer, no game) the clock free-runs as it always did.
    */
    class Pattern_Scanner_ScanIdle : public PatternScanner
    {
    public:
        Pattern_Scanner_ScanIdle();

        virtual void reset() override;

        /// scanner_tags::Ping: the scan ping just sounded - flash now, pulse now
        virtual bool onTrigger(const GameplayTag& tag) override;

        // CRGB(0.2, 0.7, 1.0)
        HSV scanColor = HSV(202.5f, 0.8f, 1.0f);
        float cycleTime = 2.0f;

        /// on the normalized 0..1 cycle clock: the ring's heartbeat
        eanim::AutomationCurve breathCurve;

        /// the obelisk's pulse, on the same clock: what one row of the tower
        /// shows from the moment the pulse reaches it. Its first key is the
        /// dark a row sits in before its turn, its last the dark after.
        eanim::AutomationCurve pulseCurve;

        /// how much of the tower this look *claims*, on the cycle clock: 1
        /// while the pulse is fresh, easing back to 0 as the next ping comes
        /// due. A row only counts once the pulse has reached it. So over an
        /// underlay (the obelisk's own look, simulated on the desk - see
        /// eanim::Underlay) the scan line climbs the tower leaving darkness
        /// behind it, as if it took the energy out of the sculpture's own
        /// colour, which then slowly comes back - until the next ping. When
        /// the pings stop the tower is the sculpture's own again. Without
        /// an underlay it changes nothing: the look is over black.
        eanim::AutomationCurve wakeCurve;

        /// how far into a cycle a row has been claimed by, once the pulse
        /// has arrived - the leading edge of the wake, in cycles
        float wakeRise = 0.04f;

        /// when in the cycle the pulse leaves the foot - the breath curve's
        /// peak, so it leaves as the ring flashes
        float flashPhase = 1.0f / 9.0f;

        /// how much of the cycle the pulse takes to climb foot to tip. With
        /// the pulse curve's length that has to fit inside one cycle, or the
        /// tip is still lit when the next pulse leaves the foot.
        float riseCycles = 0.5f;

        /// the truss: dark while the tower idles unless trussScan is on -
        /// then a scanner along it, at sweeps per second end to end and
        /// back, the dot covering this much of the truss
        bool trussScan = false;
        float sweepRate = 0.6f;
        float sweepWidth = 0.25f;

        /// how much of the breath the lap and the scanner leave behind them,
        /// so the objects never go fully dark between passes
        float floorLevel = 0.15f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
        virtual void reflectCurves(eanim::CurveBag& bag) override;

    private:
        /// a ping has landed since entry: the cycle is the game's, and it
        /// waits at its end for the next one instead of wrapping
        bool cued{false};
    };


    /* @brief scan_idle while the emergency transmission lock is active: a slow
    * red throb. */
    class Pattern_Scanner_Emergency : public PatternScanner
    {
    public:
        /// radians per second into the sine; the throb spends half of each
        /// cycle dark, so the felt pulse is this over 2 pi
        float throbRate = 2.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief scan_item_detected_filter, and the recording flow's shimmer: a
    * sine wave rising through the stage while a tag is being read.
    *
    * The python wrote sin(pixel + t*3) around the ring. The same sine now
    * reads y instead of the pixel index, with the sign turned so the crests
    * travel upward: each one crosses the ring below and then climbs the
    * obelisk, one wave through both rigs. The colour is still the whole
    * difference between the states that share it - cyan for the filter,
    * amber for a visitor's recording.
    */
    class Pattern_Scanner_DetectedWave : public PatternScanner
    {
    public:
        Pattern_Scanner_DetectedWave() = default;
        explicit Pattern_Scanner_DetectedWave(const HSV& inColor) : waveColor(inColor) {}

        // CRGB(47, 195, 224)
        HSV waveColor = HSV(190.0f, 0.79f, 0.88f);

        /// the python's numbers, reread as a wave in y: one radian per stage
        /// unit, rising at three units a second - so a crest passes any given
        /// pixel on the same 2.1s period the ring always had
        float radiansPerUnit = 1.0f;
        float unitsPerSecond = 3.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief scan_item_detected_generic / _seed: a dim blue-white shimmer.
    *
    * The python built rgb(x, x, x + 20) from a sine, which is a near-white
    * with a fixed blue floor; the same is done here in HSV directly.
    */
    class Pattern_Scanner_DetectedShimmer : public PatternScanner
    {
    public:
        /// radians per second into the shimmer's sine
        float shimmerRate = 5.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief scan_item_detected_mushroom, already discovered: a sawtooth blend
    * between a murky olive and a deep purple accent. */
    class Pattern_Scanner_DetectedMushroom : public PatternScanner
    {
    public:
        // CHSV(0.2, 0.1, 0.1) and CHSV(0.8, 0.8, 0.3)
        HSV baseColor = HSV(72.0f, 0.1f, 0.1f);
        HSV accentColor = HSV(288.0f, 0.8f, 0.3f);

        /// sawtooth passes per second between the two colours
        float blendRate = 2.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief scan_item_detected_mushroom, newly discovered: the whole ring
    * sweeps through the hue wheel together. */
    class Pattern_Scanner_DetectedMushroomNew : public PatternScanner
    {
    public:
        /// trips around the hue wheel per second
        float hueRate = 0.5f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief scan_item_success_mushroom: two slow hue drifts a quarter phase
    * apart, one flat across the stage and one rippling over it, met halfway.
    *
    * The ripple's phase was the pixel index; it is x + y now, so it runs
    * diagonally across the obelisk's face and around the ring below instead
    * of following the wiring order.
    */
    class Pattern_Scanner_SuccessMushroom : public PatternScanner
    {
    public:
        /// how fast the two hue drifts wander
        float driftRate = 1.0f;
        /// scales the x + y phase: 0 flattens the ripple to the drift alone,
        /// past 1 the diagonal bands tighten. Independent of driftRate.
        float rippleScale = 1.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief audio_playback_mushroom: a rainbow wrapped once up the stage,
    * rocking back and forth on a sine.
    *
    * The python wrapped it once around the strip; the wheel now spans the
    * stage's height, ring at one end of it and obelisk top at the other, so
    * the two rigs are slices of one rainbow rather than each wearing their
    * own.
    */
    class Pattern_Scanner_PlaybackMushroom : public PatternScanner
    {
    public:
        /// how fast the rainbow rocks back and forth
        float rockRate = 1.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief audio_playback_generic: a green-to-orange ramp scrolling along
    * the strip once a second.
    *
    * The python scrolled CRGB(b, 0.5, 0) with b rising 0..1; these stops are
    * that ramp converted at b = 0, 0.5, 1.
    */
    class Pattern_Scanner_PlaybackGeneric : public PatternScanner
    {
    public:
        HSVPalette rampPalette {
            HSV(120.0f, 1.0f, 0.5f),
            HSV(60.0f, 1.0f, 0.5f),
            HSV(30.0f, 1.0f, 1.0f),
        };

        /// ramp lengths scrolled past per second
        float scrollRate = 1.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief The whole ring breathing one colour on a sine.
    *
    * The shape three of the recording-flow states share, differing only in
    * their numbers: brightness runs floorLevel..floorLevel+gain as the sine
    * swings, at rate radians per second.
    *   record_arm    amber, quick and shallow, waiting for the rock
    *   record_saved  green, faster and brighter, the take is on disk
    *   void          purple from black, the empty stone's slow breath
    */
    class Pattern_Scanner_SinePulse : public PatternScanner
    {
    public:
        Pattern_Scanner_SinePulse(const HSV& inColor, float inRate, float inFloor, float inGain)
            : color(inColor), rate(inRate), floorLevel(inFloor), gain(inGain) {}

        HSV color;
        float rate;
        float floorLevel;
        float gain;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief record_active: a red comet orbiting the ring while tape rolls.
    *
    * The head is an angle around the ring's centre on the stage, not an
    * index along a strip. On the ring that is the python comet unchanged -
    * 12 pixels a second, an 8 pixel tail, a dim red floor behind it - and on
    * the obelisk, standing above the orbit, the same pass reads as a beam
    * sweeping across the tower once per revolution, narrower the higher it
    * reaches. One clock, both rigs, no per-rig cases.
    */
    class Pattern_Scanner_RecordComet : public PatternScanner
    {
    public:
        // CRGB(1.0, 0.05, 0.05)
        HSV cometColor = HSV(0.0f, 0.95f, 1.0f);
        float revsPerSecond = 12.0f / 35.0f;
        /// the tail, as a fraction of a revolution
        float tailFraction = 8.0f / 35.0f;

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };


    /* @brief record_countdown: three white pulses - 3, 2, 1 - before the tape
    * rolls, so a take does not start abruptly out of the arm state.
    *
    * Each count fires an AutomationCurveTrigger whose curve is one quick
    * white pulse. A node's height on the stage delays its read into that
    * curve, which is the whole sweep: the count lands at the bottom of the
    * ring and runs to the top of the obelisk in sweepSeconds, so the pulse
    * crosses the ring before it climbs the sculpture.
    */
    class Pattern_Scanner_RecordCountdown : public PatternScanner
    {
    public:
        Pattern_Scanner_RecordCountdown();

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// counts fire at 0, 1 and 2 seconds; the game holds the state for 3.
        /// Not reflected: the count is the game's choreography, and a desk
        /// retiming it here would desync the lights from the display and the
        /// take.
        float secondsPerCount = 1.0f;
        int totalCounts = 3;

        /// how long a pulse takes to climb the whole stage, kStageBottom to
        /// kStageTop
        float sweepSeconds = 0.25f;

    private:
        eanim::AutomationCurveTrigger pulse;
        int firedCount{0};
    };


    // Matrix rain, Fire2012 and the rest of the free-standing stage looks
    // live in generic_patterns.h - they render against the same stage but
    // belong to no game state.


    /* @brief One flat color. Covers the python states that just filled the
    * ring: success green, failure red, seed verdicts, and - at v 0 - the rest
    * and blackout states. */
    class Pattern_Scanner_Solid : public PatternScanner
    {
    public:
        Pattern_Scanner_Solid() = default;
        explicit Pattern_Scanner_Solid(const HSV& inColor) : color(inColor) {}

        HSV color = HSV(0.0f, 0.0f, 0.0f);

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief A State_GenericHSV that rewinds its scanner pattern's clock on
    * entry, matching the python states' getStateTimeActive() starting at zero.
    */
    class State_ScannerHSV : public State_GenericHSV
    {
    public:
        State_ScannerHSV(const char* InStateName, RelicIO* inIO, std::shared_ptr<PatternScanner> inPattern);

    protected:
        virtual void onStateChangeState(StateStatus inStatus) override;

        std::shared_ptr<PatternScanner> scannerPattern;
    };
}
