// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "scanner_patterns.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// HSVStripNode_Mapped2D, for nodeCoord's read of the stage
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

Coordinate PatternScanner::nodeCoord(const HSVStripNode* node)
{
    if (node->GetStripNodeType() == StripNodeType::MAPPED2D)
    {
        return static_cast<const HSVStripNode_Mapped2D*>(node)->coord;
    }

    // a bare strip stands in as a vertical run: its index up the y axis, so a
    // look that travels the stage still travels the strip
    return Coordinate(0.0f, static_cast<float>(node->getStripIdx()));
}

float PatternScanner::stageAlpha(const HSVStripNode* node)
{
    return std::clamp((nodeCoord(node).y - kStageBottom) / (kStageTop - kStageBottom), 0.0f, 1.0f);
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

void Pattern_Scanner_PowerUp::reflect(ecore::PropertyBag& bag)
{
    bag.add("boot_time", bootTime, 0.5f, 10.0f);
}

Pattern_Scanner_Boot::Pattern_Scanner_Boot()
{
    // the python's eight-stop table, one key per stop
    brightnessCurve.addKey(0.0f,        0.1f);
    brightnessCurve.addKey(1.0f / 7.0f, 0.8f);
    brightnessCurve.addKey(2.0f / 7.0f, 0.7f);
    brightnessCurve.addKey(3.0f / 7.0f, 1.0f);
    brightnessCurve.addKey(4.0f / 7.0f, 0.8f);
    brightnessCurve.addKey(5.0f / 7.0f, 0.5f);
    brightnessCurve.addKey(6.0f / 7.0f, 0.2f);
    brightnessCurve.addKey(1.0f,        0.0f);

    // the sixteen-stop noise table, compressed: holds and straight runs fold
    // into their endpoint keys, and the two curved shoulders - the opening
    // fall and the mid-boot spike's collapse - are easings instead of stops.
    // Within a few percent of the table everywhere, under a per-frame random
    // flicker that hides far more than that.
    noiseCurve.addKey(0.0f,          1.0f, easing_functions::EaseOutQuad);
    noiseCurve.addKey(2.0f / 15.0f,  0.4f);
    noiseCurve.addKey(4.0f / 15.0f,  0.4f, easing_functions::EaseOutQuad);
    noiseCurve.addKey(6.0f / 15.0f,  1.0f, easing_functions::EaseInQuad);
    noiseCurve.addKey(8.0f / 15.0f,  0.2f);
    noiseCurve.addKey(12.0f / 15.0f, 0.0f);
    noiseCurve.addKey(1.0f,          0.0f);
}

void Pattern_Scanner_Boot::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float bootAlpha = clamp01(timeActive / bootTime);
    const float brightness = brightnessCurve.evaluate(bootAlpha);
    const float noiseStrength = noiseCurve.evaluate(bootAlpha);

    // mix(color, black, random * noise) - a per-pixel, per-frame flicker
    // eating into the swell.
    const float flicker = 1.0f - get_random_float() * noiseStrength * noiseGain;

    inOutColor = HSV(300.0f, 1.0f, brightness * clamp01(flicker));
}

void Pattern_Scanner_Boot::reflect(ecore::PropertyBag& bag)
{
    // Two independent handles on the one swell: how long it takes, and how
    // hard the noise chews on it. Each is a curve target on its own.
    bag.add("boot_time", bootTime, 1.0f, 15.0f);
    bag.add("noise", noiseGain, 0.0f, 2.0f);
}

void Pattern_Scanner_Boot::reflectCurves(eanim::CurveBag& bag)
{
    bag.add("brightness", brightnessCurve);
    bag.add("noise", noiseCurve);
}

Pattern_Scanner_ScanIdle::Pattern_Scanner_ScanIdle()
{
    // the python's ten-stop table: a sharp swell at the top of the breath,
    // then dark for the back half - the six trailing zeros are two keys
    breathCurve.addKey(0.0f,        0.3f);
    breathCurve.addKey(1.0f / 9.0f, 1.0f);
    breathCurve.addKey(2.0f / 9.0f, 0.5f);
    breathCurve.addKey(3.0f / 9.0f, 0.2f);
    breathCurve.addKey(4.0f / 9.0f, 0.0f);
    breathCurve.addKey(1.0f,        0.0f);
}

void Pattern_Scanner_ScanIdle::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float cycleAlpha = std::fmod(timeActive, cycleTime) / cycleTime;
    const float breath = breathCurve.evaluate(cycleAlpha);

    // the ring, and anything that never said what it is: the breath
    float brightness = breath;

    const HSVStripNode_Space* spaced = eio::spaceOf(inNode);
    if (spaced != nullptr && spaced->space == NodeSpace::Obelisk)
    {
        // A beam going round the tower. The obelisk's own x is its strip,
        // 0..7 around four sides, column 7 beside column 0 - so a strip's
        // place is a fraction of a turn, the beam is another, and the
        // distance between them is taken the short way round. By side, a
        // strip answers with its side's centre, so both strips of a side
        // light together and the beam steps side to side.
        const float strips = static_cast<float>(kStageColumns);
        float here = spaced->local.x + 0.5f;
        if (bySide)
        {
            here = std::floor(spaced->local.x / 2.0f) * 2.0f + 1.0f;
        }
        const float at = std::fmod(here, strips) / strips;
        const float beam = timeActive * rotateRate - std::floor(timeActive * rotateRate);
        float away = std::fabs(at - beam);
        away = std::min(away, 1.0f - away) * strips;   // in strips, the short way
        const float lit = clamp01(1.0f - away / std::max(beamWidth, 0.01f));
        brightness = std::max(floorLevel * breath, lit * lit);
    }
    else if (spaced != nullptr && spaced->space == NodeSpace::Truss)
    {
        // The truss stays dark while the tower idles, unless asked: then a
        // scanner along it, the dot running end to end and back on a
        // triangle of time, and a par lighting by how close it is to it.
        brightness = 0.0f;
        if (trussScan)
        {
            const float phase = timeActive * sweepRate - std::floor(timeActive * sweepRate);
            const float dot = 1.0f - std::fabs(2.0f * phase - 1.0f);
            const float lit = clamp01(1.0f - std::fabs(spaced->u - dot) / std::max(sweepWidth, 0.01f));
            brightness = std::max(floorLevel * breath, lit * lit);
        }
    }

    inOutColor = scanColor;
    inOutColor.setBrightnessAlpha(scanColor.getValFloat() * brightness);
}

void Pattern_Scanner_ScanIdle::reflect(ecore::PropertyBag& bag)
{
    bag.add("cycle_time", cycleTime, 0.5f, 8.0f);
    bag.add("rotate_rate", rotateRate, 0.0f, 2.0f);
    bag.add("beam_width", beamWidth, 0.5f, 4.0f);
    bag.add("by_side", bySide);
    bag.add("truss_scan", trussScan);
    bag.add("sweep_rate", sweepRate, 0.0f, 3.0f);
    bag.add("sweep_width", sweepWidth, 0.05f, 1.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
}

void Pattern_Scanner_ScanIdle::reflectCurves(eanim::CurveBag& bag)
{
    bag.add("breath", breathCurve);
}

void Pattern_Scanner_Emergency::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    // sin goes negative half the time; the python fed that to fancy.mix which
    // pinned it, so the throb spends half its cycle dark.
    const float brightness = clamp01(std::sin(timeActive * throbRate));

    inOutColor = HSV(0.0f, 1.0f, brightness);
}

void Pattern_Scanner_Emergency::reflect(ecore::PropertyBag& bag)
{
    bag.add("throb_rate", throbRate, 0.2f, 8.0f);
}

void Pattern_Scanner_DetectedWave::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    // mix(waveColor, black, sin(y - t * 3)) - the negative half of the sine
    // pins to full color, so the wave is wide crests of color with narrow
    // dark troughs. The sign on time makes them rise: y - 3t crests move
    // toward +y, up through the ring and on up the obelisk.
    const float y = nodeCoord(inNode).y;
    const float darkAlpha = clamp01(std::sin(y * radiansPerUnit - timeActive * radiansPerUnit * unitsPerSecond));

    inOutColor = waveColor;
    inOutColor.setBrightnessAlpha(waveColor.getValFloat() * (1.0f - darkAlpha));
}

void Pattern_Scanner_DetectedWave::reflect(ecore::PropertyBag& bag)
{
    // The wave's two dimensions, deliberately uncoupled: wave_scale is how
    // tight the crests pack on the stage, rise_speed how fast they climb it.
    bag.add("wave_scale", radiansPerUnit, 0.1f, 4.0f);
    bag.add("rise_speed", unitsPerSecond, 0.0f, 12.0f);
}

void Pattern_Scanner_DetectedShimmer::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    // rgb(x, x, x + 20) out of 255, x swinging 0..51.
    const float x = (std::sin(timeActive * shimmerRate) + 1.0f) * 25.6f;

    inOutColor = HSV(240.0f, 20.0f / (x + 20.0f), (x + 20.0f) / 255.0f);
}

void Pattern_Scanner_DetectedShimmer::reflect(ecore::PropertyBag& bag)
{
    bag.add("shimmer_rate", shimmerRate, 0.5f, 12.0f);
}

void Pattern_Scanner_DetectedMushroom::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float blendAlpha = std::fmod(timeActive * blendRate, 1.0f);

    inOutColor = HSV::blend(baseColor, accentColor, blendAlpha);
}

void Pattern_Scanner_DetectedMushroom::reflect(ecore::PropertyBag& bag)
{
    bag.add("blend_rate", blendRate, 0.1f, 8.0f);
}

void Pattern_Scanner_DetectedMushroomNew::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float hueAlpha = std::fmod(timeActive * hueRate, 1.0f);

    // CHSV(hue, 200, 200)
    inOutColor = HSV(hueAlpha * 360.0f, 0.784f, 0.784f);
}

void Pattern_Scanner_DetectedMushroomNew::reflect(ecore::PropertyBag& bag)
{
    bag.add("hue_rate", hueRate, 0.05f, 2.0f);
}

void Pattern_Scanner_SuccessMushroom::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    // the ripple's phase is the node's place on the stage, not its place in
    // the wiring: x + y runs the ripple diagonally across the obelisk's face
    // and around the ring's circle below
    const Coordinate at = nodeCoord(inNode);

    const float drift = timeActive * driftRate;
    const float hueOne = (std::sin(drift) + 1.0f) * 0.5f;
    const float hueTwo = (std::sin(drift + kPi * 0.5f + (at.x + at.y) * rippleScale) + 1.0f) * 0.5f;

    inOutColor = HSV::blend(
        HSV(hueOne * 360.0f, 0.5f, 0.5f),
        HSV(hueTwo * 360.0f, 0.5f, 0.5f),
        0.5f);
}

void Pattern_Scanner_SuccessMushroom::reflect(ecore::PropertyBag& bag)
{
    bag.add("drift_rate", driftRate, 0.1f, 4.0f);
    bag.add("ripple", rippleScale, 0.0f, 4.0f);
}

void Pattern_Scanner_PlaybackMushroom::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    // the wheel spans the stage's height - ring at one end, obelisk top at
    // the other - so both rigs are slices of the one rainbow
    const float timeSinAlpha = std::fmod((std::sin(timeActive * rockRate) + 1.0f) * 0.5f, 1.0f);
    const float hueAlpha = std::fmod(timeSinAlpha + stageAlpha(inNode), 1.0f);

    inOutColor = HSV(hueAlpha * 360.0f, 1.0f, 0.5f);
}

void Pattern_Scanner_PlaybackMushroom::reflect(ecore::PropertyBag& bag)
{
    bag.add("rock_rate", rockRate, 0.1f, 4.0f);
}

void Pattern_Scanner_PlaybackGeneric::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float rampAlpha = std::fmod(timeActive * scrollRate + stripAlpha(inNode), 1.0f);

    inOutColor = rampPalette.getColor(rampAlpha);
}

void Pattern_Scanner_PlaybackGeneric::reflect(ecore::PropertyBag& bag)
{
    bag.add("scroll_rate", scrollRate, 0.1f, 4.0f);
}

void Pattern_Scanner_SinePulse::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    (void)inNode;

    const float pulse = (std::sin(timeActive * rate) + 1.0f) * 0.5f;

    inOutColor = color;
    inOutColor.setBrightnessAlpha(color.getValFloat() * (floorLevel + pulse * gain));
}

void Pattern_Scanner_SinePulse::reflect(ecore::PropertyBag& bag)
{
    // The breath's three independent handles - the states that share this
    // class (record_arm, record_saved, void) differ only in these numbers and
    // the colour, so the knobs *are* the state's identity, worth curves each.
    bag.add("rate", rate, 0.5f, 12.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 1.0f);
}

void Pattern_Scanner_RecordComet::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float headAlpha = std::fmod(timeActive * revsPerSecond, 1.0f);

    // the node's bearing around the orbit's centre, as a fraction of a turn.
    // atan2(dx, dy) puts zero at the top of the ring and turns the way the
    // pixels are numbered, so on the ring this *is* the old strip walk - and
    // a node above the orbit still has a bearing, which is what sweeps the
    // beam across the obelisk.
    const Coordinate at = nodeCoord(inNode);
    const float bearing = std::atan2(at.x - kRingCenterX, at.y - kRingCenterY) / (2.0f * kPi);

    // how far behind the head this bearing sits, wrapped around the turn
    float distance = headAlpha - bearing;
    distance -= std::floor(distance);

    // the python's clamp(1 - distance/8, 0.05, 1): a linear tail, and a 0.05
    // floor so the rest of the rig glows dim red rather than going out
    const float brightness = std::clamp(1.0f - distance / tailFraction, 0.05f, 1.0f);

    inOutColor = cometColor;
    inOutColor.setBrightnessAlpha(cometColor.getValFloat() * brightness);
}

void Pattern_Scanner_RecordComet::reflect(ecore::PropertyBag& bag)
{
    bag.add("orbit_rate", revsPerSecond, 0.05f, 2.0f);
    bag.add("tail", tailFraction, 0.02f, 1.0f);
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

    // height on the stage delays the read into the pulse's curve, which is
    // the sweep: the count lands at the bottom of the ring and the top of the
    // obelisk sees it sweepSeconds later.
    const float delay = stageAlpha(inNode) * sweepSeconds;
    inOutColor = HSV(0.0f, 0.0f, pulse.curve.evaluate(sinceCount - delay));
}

void Pattern_Scanner_RecordCountdown::reflect(ecore::PropertyBag& bag)
{
    // only the sweep: the count's timing belongs to the game - see the
    // header on secondsPerCount
    bag.add("sweep", sweepSeconds, 0.0f, 1.0f);
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
