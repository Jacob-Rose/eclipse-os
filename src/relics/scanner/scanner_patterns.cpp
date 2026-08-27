// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "scanner_patterns.h"

#include <algorithm>
#include <cmath>

// HSVStripNode_Mapped2D, for the countdown's height read
#include "../../lib/eio/strip_projection.h"

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

void Pattern_Scanner_SinePulse::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float pulse = (std::sin(timeActive * rate) + 1.0f) * 0.5f;

    inOutColor = color;
    inOutColor.setBrightnessAlpha(color.getValFloat() * (floorLevel + pulse * gain));
}

void Pattern_Scanner_RecordComet::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float headAlpha = std::fmod(timeActive * revsPerSecond, 1.0f);

    // how far behind the head this pixel sits, wrapped around the ring
    float distance = headAlpha - stripAlpha(inNode);
    if (distance < 0.0f)
    {
        distance += 1.0f;
    }

    // the python's clamp(1 - distance/8, 0.05, 1): a linear tail, and a 0.05
    // floor so the rest of the ring glows dim red rather than going out
    const float brightness = std::clamp(1.0f - distance / tailFraction, 0.05f, 1.0f);

    inOutColor = cometColor;
    inOutColor.setBrightnessAlpha(cometColor.getValFloat() * brightness);
}

Pattern_Scanner_RecordCountdown::Pattern_Scanner_RecordCountdown()
{
    // one count's shape: snap to white, fall away before the next count
    pulse.curve.addKey(0.00f, 0.0f);
    pulse.curve.addKey(0.06f, 1.0f, easing_functions::EaseOutCubic);
    pulse.curve.addKey(0.45f, 0.0f);

    // a pulse is over well before the next count fires, so a plain restart
    // never shows the step-down RestartHold exists to hide
    pulse.retriggerMode = eanim::RetriggerMode::Restart;
}

void Pattern_Scanner_RecordCountdown::reset()
{
    PatternScanner::reset();
    firedCount = 0;
    pulse.reset();
}

void Pattern_Scanner_RecordCountdown::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);

    // triggerAt with how long ago the count really landed, so a count that
    // falls mid-frame is not quantised onto the frame grid
    while (firedCount < totalCounts && timeActive >= firedCount * secondsPerCount)
    {
        pulse.triggerAt(timeActive - firedCount * secondsPerCount);
        ++firedCount;
    }

    pulse.tick(deltaTime);
}

void Pattern_Scanner_RecordCountdown::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float sinceCount = pulse.getTimeSinceTrigger();
    if (sinceCount < 0.0f)
    {
        inOutColor = HSV(0.0f, 0.0f, 0.0f);
        return;
    }

    // height delays the read into the pulse's curve, which is the sweep. A
    // node with no 2d mapping sits at height zero and pulses on the count.
    float height = 0.0f;
    if (inNode->GetStripNodeType() == StripNodeType::MAPPED2D)
    {
        height = static_cast<HSVStripNode_Mapped2D*>(inNode)->coord.y;
    }

    const float delay = clamp01(height / sweepHeight) * sweepSeconds;
    inOutColor = HSV(0.0f, 0.0f, pulse.curve.evaluate(sinceCount - delay));
}

void Pattern_Scanner_PlaybackRecording::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float breath = (std::sin(timeActive * 2.0f) + 1.0f) * 0.5f;
    const float ripple = std::sin(inNode->getStripIdx() * 0.7f + timeActive) * 0.15f;
    const float brightness = clamp01(0.2f + breath * 0.6f + ripple);

    inOutColor = playbackColor;
    inOutColor.setBrightnessAlpha(playbackColor.getValFloat() * brightness);
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
