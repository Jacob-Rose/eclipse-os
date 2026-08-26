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
#include "../../lib/eanim/generator_hsv.h"
#include "../../lib/esm/state_generic.h"

using namespace ecore;
using namespace eanim;
using namespace esm;

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
    /* @brief A scanner look: a GeneratorHSV with its own resettable clock. */
    class PatternScanner : public GeneratorHSV
    {
    public:
        virtual void reset() { timeActive = 0.0f; }
        virtual void tick(float deltaTime) override { timeActive += deltaTime; }

    protected:
        /* @brief Where this node sits along its strip, 0..1.
        *
        * The python looks indexed a 35 pixel ring; anything that used
        * pixel / n becomes this, so the look stretches to whatever strip the
        * relic actually has.
        */
        static float stripAlpha(const HSVStripNode* node);

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
    };


    /* @brief boot: a magenta swell that flickers with noise while it rises and
    * dies away, following the original keyframe tables. */
    class Pattern_Scanner_Boot : public PatternScanner
    {
    public:
        float bootTime = 7.5f;

        std::vector<float> brightnessKeys = {0.1f, 0.8f, 0.7f, 1.0f, 0.8f, 0.5f, 0.2f, 0.0f};
        std::vector<float> noiseKeys = {1.0f, 0.6f, 0.4f, 0.4f, 0.4f, 0.8f, 1.0f, 0.8f, 0.2f, 0.2f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f};

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief scan_idle: the whole ring breathes cyan on a 2 second heartbeat. */
    class Pattern_Scanner_ScanIdle : public PatternScanner
    {
    public:
        // CRGB(0.2, 0.7, 1.0)
        HSV scanColor = HSV(202.5f, 0.8f, 1.0f);
        float cycleTime = 2.0f;

        std::vector<float> idleBrightnessKeys = {0.3f, 1.0f, 0.5f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief scan_idle while the emergency transmission lock is active: a slow
    * red throb. */
    class Pattern_Scanner_Emergency : public PatternScanner
    {
    public:
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief scan_item_detected_filter: a cyan sine wave crawling along the
    * strip while the tag is being read. */
    class Pattern_Scanner_DetectedWave : public PatternScanner
    {
    public:
        // CRGB(47, 195, 224)
        HSV waveColor = HSV(190.0f, 0.79f, 0.88f);

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief scan_item_detected_generic / _seed: a dim blue-white shimmer.
    *
    * The python built rgb(x, x, x + 20) from a sine, which is a near-white
    * with a fixed blue floor; the same is done here in HSV directly.
    */
    class Pattern_Scanner_DetectedShimmer : public PatternScanner
    {
    public:
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief scan_item_detected_mushroom, already discovered: a sawtooth blend
    * between a murky olive and a deep purple accent. */
    class Pattern_Scanner_DetectedMushroom : public PatternScanner
    {
    public:
        // CHSV(0.2, 0.1, 0.1) and CHSV(0.8, 0.8, 0.3)
        HSV baseColor = HSV(72.0f, 0.1f, 0.1f);
        HSV accentColor = HSV(288.0f, 0.8f, 0.3f);

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief scan_item_detected_mushroom, newly discovered: the whole ring
    * sweeps through the hue wheel together. */
    class Pattern_Scanner_DetectedMushroomNew : public PatternScanner
    {
    public:
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief scan_item_success_mushroom: two slow hue drifts a quarter phase
    * apart, one flat across the ring and one rippling per pixel, met halfway. */
    class Pattern_Scanner_SuccessMushroom : public PatternScanner
    {
    public:
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


    /* @brief audio_playback_mushroom: a rainbow wrapped once around the strip,
    * rocking back and forth on a sine. */
    class Pattern_Scanner_PlaybackMushroom : public PatternScanner
    {
    public:
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
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

        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };


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
