// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "generic_patterns.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../../lib/eio/strip_projection.h"

using namespace scanner;
using eanim::Particle;
using eanim::ParticleSystem;

namespace
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTwoPi = 2.0f * kPi;

    float clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }

    float frac(float v)
    {
        return v - std::floor(v);
    }

    /* Kriegsman's HeatColor, in HSV bands: black to red, red to yellow,
    * yellow to white. */
    HSV heatColor(float h)
    {
        if (h < 1.0f / 3.0f)
        {
            return HSV(0.0f, 1.0f, h * 3.0f);
        }
        if (h < 2.0f / 3.0f)
        {
            return HSV((h - 1.0f / 3.0f) * 3.0f * 60.0f, 1.0f, 1.0f);
        }
        return HSV(60.0f, 1.0f - (h - 2.0f / 3.0f) * 3.0f * 0.7f, 1.0f);
    }

}

float scanner::valueNoise(float x, float y)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    float fx = x - ix;
    float fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);

    const auto corner = [&](int cx, int cy) {
        return hash01(static_cast<uint32_t>(cx * 311 + cy * 57));
    };

    return lerp(
        lerp(corner(ix, iy),     corner(ix + 1, iy),     fx),
        lerp(corner(ix, iy + 1), corner(ix + 1, iy + 1), fx),
        fy);
}

// ============================================================================
// Matrix rain
// ============================================================================

Pattern_Generic_MatrixRain::Pattern_Generic_MatrixRain()
    : drops(32)
{
}

void Pattern_Generic_MatrixRain::reset()
{
    PatternScanner::reset();
    drops.clear();
}

void Pattern_Generic_MatrixRain::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);

    // the knobs are live on the falling drops, not just the next ones
    for (Particle& drop : drops.all())
    {
        if (!drop.bAlive)
        {
            continue;
        }
        drop.velocity.y = -fallSpeed * (0.6f + 0.8f * hash01(drop.seed * 13u));
        drop.radius = dropWidth;
    }

    drops.tick(deltaTime);

    // a drop that has fallen clear - head and tail both off stage - dies,
    // and the population top-up below rebirths it at the top at a fresh x
    for (Particle& drop : drops.all())
    {
        if (drop.bAlive && drop.position.y < kStageBottom - tailLength)
        {
            drop.bAlive = false;
        }
    }

    const int want = std::clamp(static_cast<int>(dropCount), 0,
                                static_cast<int>(drops.all().size()));
    int alive = drops.aliveCount();

    // the knob came down: retire the surplus
    for (Particle& drop : drops.all())
    {
        if (alive <= want)
        {
            break;
        }
        if (drop.bAlive)
        {
            drop.bAlive = false;
            --alive;
        }
    }

    while (alive < want)
    {
        Particle* drop = drops.spawn();
        if (drop == nullptr)
        {
            break;
        }

        drop->position.x = hash01(drop->seed ^ 0x9e3779b9u) * (kStageColumns - 1);
        // staggered entry above the stage, so a burst of spawns does not
        // arrive as one rank of drops
        drop->position.y = kStageTop + tailLength + hash01(drop->seed * 7u) * 20.0f;
        drop->radius = dropWidth;
        ++alive;
    }
}

void Pattern_Generic_MatrixRain::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    float brightness = 0.0f;
    float headness = 0.0f;
    uint32_t litSeed = 0;

    for (const Particle& drop : drops.all())
    {
        if (!drop.bAlive)
        {
            continue;
        }

        // cheap rejects before any sqrt: the head reaches radius, the tail
        // hangs tailLength above the head
        const float dy = at.y - drop.position.y;
        if (dy < -drop.radius || dy > tailLength)
        {
            continue;
        }
        const float dxa = std::fabs(at.x - drop.position.x);
        if (dxa > drop.radius)
        {
            continue;
        }

        // the head: a soft radial glow around the particle itself, which is
        // what slides smoothly through the ring's arc in both axes
        const float head = ParticleSystem::glowAt(drop, at);

        // the tail: a streak up the wake, fading with distance
        const float lateral = 1.0f - dxa / drop.radius;
        const float tail = (dy > 0.0f)
            ? (1.0f - dy / tailLength) * lateral * 0.85f
            : 0.0f;

        const float glow = std::max(head, tail);
        if (glow > brightness)
        {
            brightness = glow;
            headness = head;
            litSeed = drop.seed;
        }
    }

    if (brightness <= 0.0f)
    {
        inOutColor = HSV(120.0f, 1.0f, 0.0f);
        return;
    }

    // The glyph churn, hashed over (particle, glyph cell, churn step): the
    // same answer for every node of a glyph until the step turns over. A
    // fresh random per frame read as texture on the obelisk's long tails,
    // but made the ring - two or three isolated pixels per glyph - strobe.
    const int glyphCell = static_cast<int>(std::floor(at.y - kStageBottom));
    const int churnStep = static_cast<int>(timeActive * churnRate);
    const float churnHash = hash01(litSeed
        + static_cast<uint32_t>(glyphCell * 7 + churnStep * 131));
    const float churn = 1.0f - churnHash * flicker;

    // the drop's own hue: the green, pushed round the wheel by a hash of
    // the drop so the tail and head agree, as far as hue_spread allows
    const float hue = std::fmod(120.0f + hueSpread * 360.0f * hash01(litSeed * 7u + 3u), 360.0f);

    // the head is the freshly written glyph: white-hot, never churned, and
    // blending on headness so it fades out smoothly in both axes
    inOutColor = HSV(
        hue,
        lerp(1.0f, 0.35f, headness),
        brightness * lerp(0.8f * churn, 1.0f, headness));
}

void Pattern_Generic_MatrixRain::reflect(ecore::PropertyBag& bag)
{
    bag.add("fall_speed", fallSpeed, 2.0f, 30.0f);
    bag.add("tail", tailLength, 2.0f, 30.0f);
    bag.add("drops", dropCount, 2.0f, 30.0f);
    bag.add("width", dropWidth, 0.3f, 2.0f);
    bag.add("flicker", flicker, 0.0f, 1.0f);
    bag.add("churn_rate", churnRate, 1.0f, 20.0f);
    bag.add("hue_spread", hueSpread, 0.0f, 1.0f);
}

// ============================================================================
// Fire 2012, as particles
// ============================================================================

Pattern_Generic_Fire2012::Pattern_Generic_Fire2012()
    : flames(64)
{
}

void Pattern_Generic_Fire2012::reset()
{
    PatternScanner::reset();
    flames.clear();
    spawnDebt = 0.0f;
}

void Pattern_Generic_Fire2012::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);

    // ignition: a rate, not a per-frame coin flip, so the fire's density
    // does not depend on the frame rate
    spawnDebt += deltaTime * sparking * std::max(emitter, 0.0f) * 40.0f;
    while (spawnDebt >= 1.0f)
    {
        spawnDebt -= 1.0f;

        Particle* flame = flames.spawn();
        if (flame == nullptr)
        {
            break;
        }

        // anywhere along the floor - a continuous line, which is the point
        // of the particles: the ignition line is the foot of the stage, not
        // eight columns. The truss stands on the same line, so it burns too.
        const float x = hash01(flame->seed ^ 0xabcdef01u) * (kStageColumns - 1);
        flame->position = Coordinate(x, kStageBottom);
        flame->velocity.y = riseSpeed * (0.7f + 0.6f * hash01(flame->seed * 3u));
        flame->radius = 1.4f + 1.6f * hash01(flame->seed * 5u);

        // cooling is Fire2012's knob for how fast heat dies; here that is a
        // flame's lifetime - cooling 30 burns ~2-4s of rise, 100 guts out
        // just above the bed
        flame->lifetime = (90.0f / std::max(cooling, 1.0f))
            * (0.7f + 0.6f * hash01(flame->seed * 11u));
    }

    // the wander: each flame sways on its own rate, spread scaling how far
    for (Particle& flame : flames.all())
    {
        if (!flame.bAlive)
        {
            continue;
        }
        const float sway = 1.5f + 2.0f * hash01(flame.seed * 17u);
        const float phase = hash01(flame.seed * 23u) * kTwoPi;
        flame.velocity.x = std::sin(flame.age * sway + phase) * spread * 20.0f;
    }

    flames.tick(deltaTime);

    // gone past the top: nothing above the stage to burn
    for (Particle& flame : flames.all())
    {
        if (flame.bAlive && flame.position.y > kStageTop + flame.radius)
        {
            flame.bAlive = false;
        }
    }
}

void Pattern_Generic_Fire2012::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    float heat = 0.0f;

    for (const Particle& flame : flames.all())
    {
        if (!flame.bAlive)
        {
            continue;
        }

        // cheap rejects before the sqrt in glowAt
        if (std::fabs(at.x - flame.position.x) > flame.radius
            || std::fabs(at.y - flame.position.y) > flame.radius)
        {
            continue;
        }

        const float glow = ParticleSystem::glowAt(flame, at);
        if (glow <= 0.0f)
        {
            continue;
        }

        // a flame swells in over its first beat - born at full heat it would
        // pop into existence on the arc, and nineteen pops a second is a
        // strobe - then dies dark: age runs its heat down, and the palette
        // walks it back through yellow, orange and red
        const float born = std::min(flame.age / 0.3f, 1.0f);
        const float heatFrac = 1.0f - flame.age / std::max(flame.lifetime, 0.001f);
        heat = std::max(heat, glow * born * heatFrac);
    }

    // the ember bed: the floor as a distance field, glowing with a slow
    // lerped flicker per stretch of it - the line the risers are born from
    {
        const float fromFloor = std::fabs(at.y - kStageBottom);
        const float bed = 1.0f - fromFloor / 1.5f;

        if (bed > 0.0f)
        {
            const int segment = static_cast<int>(at.x * 1.5f);
            const float breath = timeActive * 2.0f;
            const int step = static_cast<int>(breath);
            const float flicker = lerp(
                hash01(static_cast<uint32_t>(segment * 31 + step * 7)),
                hash01(static_cast<uint32_t>(segment * 31 + (step + 1) * 7)),
                breath - step);

            heat = std::max(heat, bed * bed * (0.30f + 0.25f * flicker));
        }
    }

    inOutColor = heatColor(clamp01(heat));
}

void Pattern_Generic_Fire2012::reflect(ecore::PropertyBag& bag)
{
    bag.add("cooling", cooling, 10.0f, 100.0f);
    bag.add("sparking", sparking, 0.0f, 1.0f);
    bag.add("spread", spread, 0.0f, 0.5f);
    bag.add("rise", riseSpeed, 4.0f, 30.0f);
    bag.add("emitter", emitter, 0.0f, 2.0f);
}

// ============================================================================
// Flow
// ============================================================================

void Pattern_Generic_Flow::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    // WLED's zones, on the stage's height: each band carries the palette as
    // a gradient, every other band flipped, all of them scrolling - so
    // neighbouring bands stream against each other.
    const float zones = std::max(zoneCount, 1.0f);
    const float zoned = stageAlpha(inNode) * zones;

    const int zone = static_cast<int>(zoned);
    float local = zoned - zone;
    if ((zone & 1) != 0)
    {
        local = 1.0f - local;
    }

    inOutColor = flowPalette.getColor(frac(local + timeActive * flowSpeed));
}

void Pattern_Generic_Flow::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", flowSpeed, 0.02f, 2.0f);
    bag.add("zones", zoneCount, 1.0f, 8.0f);
}

// ============================================================================
// Lake
// ============================================================================

void Pattern_Generic_Lake::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    // two waves at different scales, drifting against each other. Diagonal
    // across (x, y), so the interference wanders over the stage instead of
    // marching along the wiring.
    const float driftOne = std::sin(timeActive * 0.60f * waveSpeed) * 2.0f;
    const float driftTwo = std::sin(timeActive * 0.43f * waveSpeed) * 2.0f;

    const float waveOne = std::cos(at.y * 0.55f + at.x * 0.25f + driftOne) * 0.5f + 0.5f;

    // WLED's cubicwave: a sine with its crests fattened by cubing
    const float raw = std::sin(at.y * 0.85f - at.x * 0.35f + driftTwo);
    const float waveTwo = raw * raw * raw * 0.5f + 0.5f;

    const float surface = waveOne * 0.5f + waveTwo * 0.5f;

    // the slow threshold that swallows the troughs: where the water goes
    // deep-dark between glints
    const float floorLine = (std::sin(timeActive * 0.48f * waveSpeed) + 1.0f) * 0.5f * 0.31f;
    const float glint = clamp01(surface - floorLine);

    inOutColor = lakePalette.getColor(surface * 0.94f);
    inOutColor.setBrightnessAlpha(inOutColor.getValFloat() * (0.08f + 0.92f * glint));
}

void Pattern_Generic_Lake::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", waveSpeed, 0.1f, 3.0f);
}

// ============================================================================
// Pacifica
// ============================================================================

void Pattern_Generic_Pacifica::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    // Four layers of swell, each its own spatial scale, speed and direction
    // across (x, y). Weights sum to one, so the stack stays 0..1.
    struct Layer { float scaleY; float scaleX; float rate; float weight; };
    static constexpr Layer layers[] = {
        { 0.30f,  0.12f,  0.23f, 0.35f },
        { 0.45f, -0.17f, -0.31f, 0.30f },
        { 0.80f,  0.25f,  0.47f, 0.20f },
        { 1.30f, -0.40f, -0.61f, 0.15f },
    };

    float swell = 0.0f;
    for (const Layer& layer : layers)
    {
        const float angle = at.y * layer.scaleY + at.x * layer.scaleX
            + timeActive * layer.rate * waveSpeed * kTwoPi * 0.2f;
        swell += (std::sin(angle) + 1.0f) * 0.5f * layer.weight;
    }

    // the ocean: deep blue troughs through green-blue crests, with a floor
    // so even the troughs stay water rather than black
    const float hue = 210.0f - swell * 60.0f;              // 210 deep -> 150 crest
    const float value = 0.25f + swell * 0.75f;

    inOutColor = HSV(hue, 0.9f, value);

    // whitecaps: where the swell stacks past the cap, foam lifts the colour
    if (swell > capLevel)
    {
        const float foam = clamp01((swell - capLevel) / std::max(1.0f - capLevel, 0.01f));
        inOutColor = HSV::blend(inOutColor, HSV(180.0f, 0.15f, 1.0f), foam);
    }
}

void Pattern_Generic_Pacifica::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", waveSpeed, 0.1f, 3.0f);
    bag.add("caps", capLevel, 0.4f, 1.0f);
}

// ============================================================================
// Phased
// ============================================================================

void Pattern_Generic_Phased::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    // the breathing: the spatial frequency itself rides a slower sine, so
    // the bands stretch and squeeze as they travel
    const float breath = std::sin(timeActive * 0.35f * waveSpeed);
    const float freq = density * (1.0f + 0.6f * breath);

    const float wave = std::sin(at.y * freq + at.x * 0.3f + timeActive * 2.2f * waveSpeed);

    // cubed for contrast: WLED's phased spends most of its cycle dark with
    // bright bands sweeping through, and a raw sine reads as a wash
    const float lifted = (wave + 1.0f) * 0.5f;
    const float brightness = lifted * lifted * lifted;

    inOutColor = waveColor;
    inOutColor.setBrightnessAlpha(waveColor.getValFloat() * brightness);
}

void Pattern_Generic_Phased::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", waveSpeed, 0.1f, 3.0f);
    bag.add("density", density, 0.1f, 2.0f);
    bag.add("color", waveColor);
}

// ============================================================================
// Saw
// ============================================================================

void Pattern_Generic_Saw::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float ramp = frac(stageAlpha(inNode) * std::max(teeth, 0.1f)
        - timeActive * sawSpeed);

    inOutColor = sawColor;
    inOutColor.setBrightnessAlpha(sawColor.getValFloat() * ramp);
}

void Pattern_Generic_Saw::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", sawSpeed, 0.05f, 3.0f);
    bag.add("teeth", teeth, 0.5f, 8.0f);
    bag.add("color", sawColor);
}

// ============================================================================
// Spots Fade
// ============================================================================

void Pattern_Generic_SpotsFade::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    float brightness = 0.0f;
    float hueJitter = 0.0f;

    for (int spot = 0; spot < static_cast<int>(spotCount); ++spot)
    {
        // each spot has its own cycle length and phase, so the field never
        // pulses in lockstep
        const float cycleSeconds = fadeSeconds
            * (0.75f + 0.5f * hash01(static_cast<uint32_t>(spot * 5 + 1)));
        const float total = timeActive / cycleSeconds
            + hash01(static_cast<uint32_t>(spot * 5 + 2));
        const int cycle = static_cast<int>(total);
        const float phase = total - cycle;

        // swell in, fade out
        const float fade = std::sin(phase * kPi);

        // a fresh spot lands somewhere new each cycle
        const float spotX = hash01(static_cast<uint32_t>(spot * 617 + cycle * 41 + 3))
            * (kStageColumns - 1);
        const float spotY = kStageBottom
            + hash01(static_cast<uint32_t>(spot * 617 + cycle * 41 + 4))
            * (kStageTop - kStageBottom);

        const float dx = at.x - spotX;
        const float dy = at.y - spotY;
        const float falloff = 1.0f - std::sqrt(dx * dx + dy * dy) / radius;
        if (falloff <= 0.0f)
        {
            continue;
        }

        const float glow = falloff * falloff * fade;
        if (glow > brightness)
        {
            brightness = glow;
            hueJitter = (hash01(static_cast<uint32_t>(spot * 617 + cycle * 41 + 5)) - 0.5f) * 40.0f;
        }
    }

    inOutColor = spotColor;
    inOutColor.setHueDegree(spotColor.getHueFloat() + hueJitter);
    inOutColor.setBrightnessAlpha(spotColor.getValFloat() * brightness);
}

void Pattern_Generic_SpotsFade::reflect(ecore::PropertyBag& bag)
{
    bag.add("spots", spotCount, 1.0f, 16.0f);
    bag.add("radius", radius, 1.0f, 12.0f);
    bag.add("fade", fadeSeconds, 0.5f, 10.0f);
    bag.add("color", spotColor);
}

// ============================================================================
// TwinkleUp
// ============================================================================

void Pattern_Generic_TwinkleUp::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    // a star per half-unit patch of the stage: identity comes from where
    // the node *is*, never from its wiring order, so the field lays evenly
    // over the ring's arc and the obelisk's face
    const int ix = static_cast<int>(std::floor(at.x * 2.0f));
    const int iy = static_cast<int>(std::floor(at.y * 2.0f));
    const uint32_t star = static_cast<uint32_t>(ix * 193 + iy * 7 + 12345);

    if (hash01(star) > density)
    {
        inOutColor = HSV(0.0f, 0.0f, 0.0f);
        return;
    }

    const float rate = 0.5f + 1.5f * hash01(star * 3u);
    const float phase = hash01(star * 5u) * kTwoPi;
    const float wave = std::sin(timeActive * twinkleSpeed * rate * kPi + phase);

    // clipped and squared: a star spends most of its time dark and blinks
    // through, rather than breathing like a wash
    const float blink = clamp01(wave);
    const float brightness = blink * blink;

    inOutColor = starColor;
    inOutColor.setHueDegree(starColor.getHueFloat()
        + (hash01(star * 7u) - 0.5f) * 30.0f);
    inOutColor.setBrightnessAlpha(starColor.getValFloat() * brightness);
}

void Pattern_Generic_TwinkleUp::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", twinkleSpeed, 0.1f, 4.0f);
    bag.add("density", density, 0.05f, 1.0f);
    bag.add("color", starColor);
}

// ============================================================================
// Waterfall
// ============================================================================

void Pattern_Generic_Waterfall::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    // two octaves of value noise scrolled downward: the sheets. Sampling at
    // y + t*speed moves the features toward -y, which is the fall.
    const float scroll = timeActive * fallSpeed;
    const float water =
        0.65f * valueNoise(at.x * 0.8f, (at.y + scroll) * 0.22f)
      + 0.35f * valueNoise(at.x * 1.9f + 37.0f, (at.y + scroll * 1.35f) * 0.45f);

    // brighter where the water bunches; whiter at the crests, like foam
    const float body = water * water;
    const float foam = clamp01((water - 0.7f) / 0.3f);

    inOutColor = waterColor;
    inOutColor.setSaturationAlpha(waterColor.getSatFloat() * (1.0f - foam * 0.8f));
    inOutColor.setBrightnessAlpha(waterColor.getValFloat() * (0.06f + 0.94f * body));
}

void Pattern_Generic_Waterfall::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", fallSpeed, 2.0f, 30.0f);
    bag.add("color", waterColor);
}

// ============================================================================
// Color Clouds
// ============================================================================

void Pattern_Generic_ColorClouds::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const Coordinate at = nodeCoord(inNode);

    const float t = timeActive * 0.08f * driftSpeed;
    const float s = cloudScale;

    // the cloud bodies: two octaves drifting in different directions, so
    // patches form and dissolve rather than sliding as one sheet
    const float shape =
        0.6f * valueNoise(at.x * 0.35f * s + t * 2.1f, at.y * 0.18f * s - t * 1.3f)
      + 0.4f * valueNoise(at.x * 0.80f * s - t * 1.7f + 53.0f, at.y * 0.40f * s + t * 0.9f);

    // their colour: a separate, slower field, plus a whole-wheel wander so
    // the palette itself changes over the minutes
    const float hueField = valueNoise(at.x * 0.22f * s + 91.0f + t * 1.1f,
                                      at.y * 0.12f * s + 17.0f - t * 0.7f);
    const float hue = std::fmod(hueField * 720.0f + timeActive * 4.0f * driftSpeed, 360.0f);

    const float body = shape * std::sqrt(shape);   // shape^1.5: deeper gaps

    inOutColor = HSV(hue, 0.75f, 0.12f + 0.88f * clamp01(body));
}

void Pattern_Generic_ColorClouds::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", driftSpeed, 0.1f, 4.0f);
    bag.add("scale", cloudScale, 0.3f, 3.0f);
}

// ============================================================================
// Rainbow
// ============================================================================

void Pattern_Generic_Rainbow::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float hueAlpha = frac(stageAlpha(inNode) * wheelCount
        + timeActive * wheelSpeed);

    inOutColor = HSV(hueAlpha * 360.0f, 1.0f, 1.0f);
}

void Pattern_Generic_Rainbow::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", wheelSpeed, 0.02f, 1.0f);
    bag.add("wheels", wheelCount, 0.25f, 4.0f);
}

// ============================================================================
// Chase
// ============================================================================

void Pattern_Generic_Chase::render(HSVStripNode* inNode, HSV& inOutColor) const
{
    const float head = frac(timeActive * lapSpeed);

    // how far below the head this node sits, wrapped over the stage
    float behind = head - stageAlpha(inNode);
    behind -= std::floor(behind);

    const float fade = std::max(0.0f, 1.0f - behind / std::max(tailFraction, 0.01f));

    inOutColor = chaseColor;
    inOutColor.setBrightnessAlpha(chaseColor.getValFloat() * fade * fade);
}

void Pattern_Generic_Chase::reflect(ecore::PropertyBag& bag)
{
    bag.add("speed", lapSpeed, 0.05f, 3.0f);
    bag.add("tail", tailFraction, 0.05f, 1.0f);
    bag.add("color", chaseColor);
}
