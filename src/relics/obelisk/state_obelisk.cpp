// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_obelisk.h"

#include <algorithm>
#include <cmath>

#include "../../lib/ecore/math.h"
#include "../../lib/ecore/logging.h"
#include "../../kits/palettes.h"

#include "../../imgs/eclipse.h"

using namespace eanim;
using namespace ecore;
using namespace ecore::log;

#define OBELISK_DEBUG_ENABLED DEBUG_LOGGING_ENABLED && 0

int getSideIndex(HSVStripNode_Mapped2D* inNode)
{
    int sideIdx = inNode->coord.x / 2; // two strips per side

#if ERROR_CHECKING_ENABLED
    if(sideIdx >= 4)
    {
        std::string str = "Pattern_Obelisk_Theater::render - sideIdx out of range: " + std::to_string(sideIdx) + " >= 4";
        //dbgLog(str.c_str(), Verbosity::Warning);
        return 0;
    }
#endif

    return sideIdx;
}

Pattern_Obelisk_FourSeasons::Pattern_Obelisk_FourSeasons()
{
    coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    coreNoise.noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Manhattan);
    coreNoise.noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2);
    coreNoise.noise.SetFrequency(0.05f);
    coreNoise.noise.SetCellularJitter(1.0f);
    coreNoise.timeScale = 0.33f;
    coreNoise.imageScaleX = 1.0f;
    coreNoise.imageScaleY = 1.0f;
}

void Pattern_Obelisk_FourSeasons::tick(float deltaTime)
{
    coreNoise.tick(deltaTime);
}

void Pattern_Obelisk_FourSeasons::reflectState(ecore::PropertyBag& bag)
{
    // The whole look is the field's clock: same time, same picture.
    coreNoise.reflectState(bag, "noise.");
}

void Pattern_Obelisk_FourSeasons::render(HSVStripNode* inNode, HSV& inOutColor) const
{
#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ start");
#endif
#if ERROR_CHECKING_ENABLED
    if(inNode->GetStripNodeType() != StripNodeType::MAPPED2D)
    {
        dbgLog("four-seasons ~ only supports mapped 2d nodes", Verbosity::Error);
    }
#endif

    HSV outColor;

    HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
    int x = castedNode->coord.x;
    int y = castedNode->coord.y;
    int sideIdx = getSideIndex(castedNode);

#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ mapped coords");
#endif

    float noiseAlpha = coreNoise.evaluate(x, y);

#if OBELISK_DEBUG_ENABLED
    dbgLog("four-seasons ~ noise");
#endif
    inOutColor = palettes[sideIdx].getColor(noiseAlpha);
}

Pattern_Obelisk_Theater::Pattern_Obelisk_Theater()
{
    lfo.width = 2.f;
    lfo.speed = -1.5f;

    paletteLFO.speed = 0.1f;

}

void Pattern_Obelisk_Theater::reflectState(ecore::PropertyBag& bag)
{
    lfo.reflectState(bag, "lfo.");
    paletteLFO.reflectState(bag, "palette.");
}

void Pattern_Obelisk_Theater::tick(float deltaTime)
{
    lfo.tick(deltaTime);
    paletteLFO.tick(deltaTime);
}

void Pattern_Obelisk_Theater::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    HSV outColor;
    
    HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
    int x = castedNode->coord.x;
    int y = castedNode->coord.y;
    int sideIdx = getSideIndex(castedNode);

    float alpha = lfo.evaluate(x);
    
    inOutColor = palettes[3].getColor(alpha);
}

Pattern_Obelisk_Monocolor::Pattern_Obelisk_Monocolor()
{
}

void Pattern_Obelisk_Monocolor::render(HSVStripNode *inNode, HSV &inOutColor) const
{
    inOutColor = color;
}

void Pattern_Obelisk_Monocolor::reflect(ecore::PropertyBag& bag)
{
    bag.add("color", color);
}

Pattern_Obelisk_Blobs::Pattern_Obelisk_Blobs()
{
    // the seasons look's field, with the same kind of cells
    noise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noise.noise.SetFrequency(scale);
    noise.timeScale = 0.33f;
    noise.imageScaleX = 1.0f;
    noise.imageScaleY = 1.0f;
}

void Pattern_Obelisk_Blobs::reflectState(ecore::PropertyBag& bag)
{
    noise.reflectState(bag, "noise.");
}

void Pattern_Obelisk_Blobs::tick(float deltaTime)
{
    noise.tick(deltaTime);
}

void Pattern_Obelisk_Blobs::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);

    // simplex comes out -1..1; 0..1 is what a threshold wants
    const float field = 0.5f + 0.5f * noise.evaluate(castedNode->coord.x, castedNode->coord.y);

    // Everything above the threshold is accent, and the threshold sits so
    // that `coverage` of the field's range is above it. The edge is a
    // smoothstep `softness` wide around it: at 0 a blob is a hard cell, at 1
    // the whole field is one gradient between the two colours.
    const float threshold = 1.0f - coverage;
    const float halfEdge = 0.5f * softness + 0.001f;
    float t = std::clamp((field - (threshold - halfEdge)) / (2.0f * halfEdge), 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);

    inOutColor = HSV::blend(primary, accent, t);
}

void Pattern_Obelisk_Blobs::reflect(ecore::PropertyBag& bag)
{
    bag.add("primary", primary);
    bag.add("accent", accent);
    bag.add("speed", noise.timeScale, 0.0f, 2.0f);
    bag.add("scale", scale, 0.01f, 0.3f, [this] { noise.noise.SetFrequency(scale); });
    bag.add("coverage", coverage, 0.0f, 1.0f);
    bag.add("softness", softness, 0.0f, 1.0f);
}

Pattern_Obelisk_Prism::Pattern_Obelisk_Prism()
{
    // the blobs' field: same noise, same drift
    noise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noise.noise.SetFrequency(scale);
    noise.timeScale = 0.33f;
    noise.imageScaleX = 1.0f;
    noise.imageScaleY = 1.0f;
    updateHead();
}

void Pattern_Obelisk_Prism::reflectState(ecore::PropertyBag& bag)
{
    noise.reflectState(bag, "noise.");
    bag.addState("spin.time", spinTime, [this] { updateHead(); });
    bag.addState("hue.time", hueTime);
}

void Pattern_Obelisk_Prism::tick(float deltaTime)
{
    noise.tick(deltaTime);

    // both clocks wrap, so an installation left running for weeks keeps
    // the precision it had in its first minute
    const float loop = std::max(loopSeconds, 0.5f);
    spinTime = std::fmod(spinTime + deltaTime, loop);
    hueTime = std::fmod(hueTime + deltaTime / std::max(hueCycle, 0.5f), 1.0f);

    updateHead();
}

void Pattern_Obelisk_Prism::updateHead()
{
    constexpr float kTau = 6.28318530718f;

    // speed(t) = base * (1 - depth * cos(2 pi t / P)): slowest at the loop's
    // ends, fastest in its middle. Its integral is the head's angle:
    //   turn(t) = base * (t - depth * P / (2 pi) * sin(2 pi t / P))
    // and with base * P a whole number of laps, turn(P) is a whole number
    // of turns - the wrap of spinTime at P is seamless.
    const float loop = std::max(loopSeconds, 0.5f);
    const float wholeLaps = std::max(1.0f, std::round(laps));
    const float base = wholeLaps / loop;
    const float phase = kTau * spinTime / loop;
    const float turn = base * (spinTime - std::clamp(depth, 0.0f, 1.0f) * loop / kTau * std::sin(phase));
    headTurn = turn - std::floor(turn);
}

void Pattern_Obelisk_Prism::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
    const float x = castedNode->coord.x;
    const float y = castedNode->coord.y;

    // where this column sits round the tower, in turns: eight columns, two
    // to a side, in the order the sides go round. The twist lags the tip
    // behind the foot, for a spiral.
    const float height = y / static_cast<float>(WALL_SIDE_LENGTH - 1);
    const float around = x / 8.0f + twist * height;

    // the rainbow round the sides, the field's hue moving through it, the
    // whole wheel drifting. A whole number of wheels round wraps clean.
    const float field = 0.5f + 0.5f * noise.evaluate(x, y);
    float hue = (hueTime + (x / 8.0f) * hueAround + field * hueSpread) * 360.0f;
    hue -= 360.0f * std::floor(hue / 360.0f);

    // the nearest sweep ahead of this column: how far behind its head we
    // are, in turns, folded onto one arm's share of the turn
    const float armCount = std::max(1.0f, std::round(arms));
    float behind = (headTurn - around) * armCount;
    behind -= std::floor(behind);
    behind /= armCount;

    // a tail that falls off squared, so the head reads as a head; a sliver
    // ahead of it softens the edge it would otherwise step onto a column with
    const float tailTurns = std::max(tail, 0.01f);
    float sweep = std::max(0.0f, 1.0f - behind / tailTurns);
    sweep *= sweep;
    const float ahead = 1.0f / armCount - behind;
    sweep = std::max(sweep, std::max(0.0f, 1.0f - ahead / 0.04f));

    // the sweep brightens the field and washes it toward white at the head
    const float level = baseLevel + (1.0f - baseLevel) * sweep;
    inOutColor = HSV(hue, 1.0f - 0.45f * sweep * sweep, level);
}

void Pattern_Obelisk_Prism::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", noise.timeScale, 0.0f, 2.0f);
    bag.add("scale", scale, 0.01f, 0.3f, [this] { noise.noise.SetFrequency(scale); });
    bag.add("hue_around", hueAround, 0.0f, 3.0f);
    bag.add("hue_spread", hueSpread, 0.0f, 1.0f);
    bag.add("hue_cycle", hueCycle, 2.0f, 120.0f);
    bag.add("base", baseLevel, 0.0f, 1.0f);
    bag.add("loop_seconds", loopSeconds, 2.0f, 60.0f, [this] { updateHead(); });
    bag.add("laps", laps, 1.0f, 20.0f, [this] { updateHead(); });
    bag.add("depth", depth, 0.0f, 1.0f, [this] { updateHead(); });
    bag.add("arms", arms, 1.0f, 4.0f);
    bag.add("tail", tail, 0.02f, 1.0f);
    bag.add("twist", twist, -1.0f, 1.0f);
}
