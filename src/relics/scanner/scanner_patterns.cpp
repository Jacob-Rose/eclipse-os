// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "scanner_patterns.h"

#include <algorithm>
#include <cmath>

using namespace scanner;

namespace
{
    constexpr float kPi = 3.14159265358979f;

    float clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }
}

float PatternScanner::stripAlpha(const HSVStripNode* node)
{
    const HSVStrip* strip = node->getStripSegment()->getParentStrip();
    const uint16_t length = strip->getLength();
    return length > 0 ? static_cast<float>(node->getStripIdx()) / static_cast<float>(length) : 0.0f;
}

void Pattern_Scanner_PowerUp::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    // Every pixel gets a fixed slot in 0..1; it stays dark until boot progress
    // passes its slot, then holds green at that brightness - the python's
    // pixel_cache, made deterministic.
    const float visitAlpha = std::fmod(inNode->getStripIdx() * 0.6180339887f, 1.0f);
    const float progress = clamp01(timeActive / bootTime);

    if (visitAlpha <= progress)
    {
        inOutColor = HSV(120.0f, 1.0f, visitAlpha);
    }
    else
    {
        inOutColor = HSV(0.0f, 0.0f, 0.0f);
    }
}

void Pattern_Scanner_Boot::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float bootAlpha = clamp01(timeActive / bootTime);
    const float brightness = lerp_keyframes(bootAlpha, brightnessKeys);
    const float noiseStrength = lerp_keyframes(bootAlpha, noiseKeys);

    // mix(color, black, random * noise) - a per-pixel, per-frame flicker
    // eating into the swell.
    const float flicker = 1.0f - get_random_float() * noiseStrength;

    inOutColor = HSV(300.0f, 1.0f, brightness * clamp01(flicker));
}

void Pattern_Scanner_ScanIdle::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float cycleAlpha = std::fmod(timeActive, cycleTime) / cycleTime;
    const float brightness = lerp_keyframes(cycleAlpha, idleBrightnessKeys);

    inOutColor = scanColor;
    inOutColor.setBrightnessAlpha(scanColor.getValFloat() * brightness);
}

void Pattern_Scanner_Emergency::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    // sin goes negative half the time; the python fed that to fancy.mix which
    // pinned it, so the throb spends half its cycle dark.
    const float brightness = clamp01(std::sin(timeActive * 2.0f));

    inOutColor = HSV(0.0f, 1.0f, brightness);
}

void Pattern_Scanner_DetectedWave::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    // mix(waveColor, black, sin(pixel + t * 3)) - the negative half of the
    // sine pins to full color, so the wave is wide crests of cyan with narrow
    // dark troughs sliding along the strip.
    const float darkAlpha = clamp01(std::sin(inNode->getStripIdx() + timeActive * 3.0f));

    inOutColor = waveColor;
    inOutColor.setBrightnessAlpha(waveColor.getValFloat() * (1.0f - darkAlpha));
}

void Pattern_Scanner_DetectedShimmer::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    // rgb(x, x, x + 20) out of 255, x swinging 0..51.
    const float x = (std::sin(timeActive * 5.0f) + 1.0f) * 25.6f;

    inOutColor = HSV(240.0f, 20.0f / (x + 20.0f), (x + 20.0f) / 255.0f);
}

void Pattern_Scanner_DetectedMushroom::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float blendAlpha = std::fmod(timeActive * 2.0f, 1.0f);

    inOutColor = HSV::blend(baseColor, accentColor, blendAlpha);
}

void Pattern_Scanner_DetectedMushroomNew::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float hueAlpha = std::fmod(timeActive * 0.5f, 1.0f);

    // CHSV(hue, 200, 200)
    inOutColor = HSV(hueAlpha * 360.0f, 0.784f, 0.784f);
}

void Pattern_Scanner_SuccessMushroom::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float hueOne = (std::sin(timeActive) + 1.0f) * 0.5f;
    const float hueTwo = (std::sin(timeActive + kPi * 0.5f + inNode->getStripIdx()) + 1.0f) * 0.5f;

    inOutColor = HSV::blend(
        HSV(hueOne * 360.0f, 0.5f, 0.5f),
        HSV(hueTwo * 360.0f, 0.5f, 0.5f),
        0.5f);
}

void Pattern_Scanner_PlaybackMushroom::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float timeSinAlpha = std::fmod((std::sin(timeActive) + 1.0f) * 0.5f, 1.0f);
    const float hueAlpha = std::fmod(timeSinAlpha + stripAlpha(inNode), 1.0f);

    inOutColor = HSV(hueAlpha * 360.0f, 1.0f, 0.5f);
}

void Pattern_Scanner_PlaybackGeneric::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float rampAlpha = std::fmod(timeActive + stripAlpha(inNode), 1.0f);

    inOutColor = rampPalette.getColor(rampAlpha);
}

void Pattern_Scanner_Solid::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    inOutColor = color;
}

State_ScannerHSV::State_ScannerHSV(const char* InStateName, RelicIO* inIO, std::shared_ptr<PatternScanner> inPattern)
    : State_GenericHSV(InStateName, inIO)
    , scannerPattern(inPattern)
{
    setGenerator(inPattern);
}

void State_ScannerHSV::onStateChangeState(StateStatus inStatus)
{
    // Entered fresh: either the machine is blending toward us, or we were set
    // active directly from Off. Becoming Active at the *end* of a blend also
    // lands here as Active, but from TransitionIn - rewinding then would
    // visibly restart the look mid-flight, so it is excluded.
    const bool bEntering = inStatus == StateStatus::TransitionIn
        || (inStatus == StateStatus::Active && GetStatus() == StateStatus::Off);

    State_GenericHSV::onStateChangeState(inStatus);

    if (bEntering && scannerPattern)
    {
        scannerPattern->reset();
    }
}
