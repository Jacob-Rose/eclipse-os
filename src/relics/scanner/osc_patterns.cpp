// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "osc_patterns.h"

#include <algorithm>
#include <cmath>

#include <ecore/color_mix.h>

using namespace scanner;
using namespace ebeat;
using ecore::blendRgb;

namespace
{
    float glide(float current, float target, float seconds, float deltaTime)
    {
        if (seconds <= 0.0f || deltaTime <= 0.0f)
        {
            return target;
        }
        return current + (target - current) * (1.0f - std::exp(-deltaTime / seconds));
    }

    /// Toward a target, quicker up than down: a band's swell in over
    /// `attack`, out over `release`.
    float ease(float current, float target, float attack, float release, float deltaTime)
    {
        return glide(current, target, target > current ? attack : release, deltaTime);
    }

    float smooth(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /// the origin line as a share of the stage: where the ring sits, and
    /// where a beat starts from
    constexpr float kOriginAlpha =
        (scanner::kStageOriginY - scanner::kStageBottom) / (scanner::kStageTop - scanner::kStageBottom);
}

// ============================================================================
// the shared part
// ============================================================================

OscHost& scanner::sharedOscHost()
{
    static OscHost host;
    return host;
}

void Pattern_Osc_Look::reset()
{
    PatternScanner::reset();
    // the levels are not reset: they are the sender's, and a look coming up
    // mid-song should come up at the song's level, not climb to it
    sinceHit = 1000.0f;
    hits = 0;
    hitNow = false;
}

void Pattern_Osc_Look::tick(float deltaTime)
{
    PatternScanner::tick(deltaTime);
    if (bus == nullptr)
    {
        bus = &sharedAudioLevel();
    }
    const float dt = std::max(deltaTime, 0.0f);

    const double now = nowSeconds();
    const float scale = std::max(gain, 0.0f);
    const auto read = [&](AudioChannel channel) { return std::clamp(bus->get(channel, now) * scale, 0.0f, 1.0f); };
    level = glide(level, read(AudioChannel::Level), slew, dt);
    sub = glide(sub, read(AudioChannel::Sub), slew, dt);
    bass = glide(bass, read(AudioChannel::Bass), slew, dt);
    mid = glide(mid, read(AudioChannel::Mid), slew, dt);
    high = glide(high, read(AudioChannel::High), slew, dt);
    // presence: the same bands, slow - how much of each is in the room,
    // which is what a jam has more of than it has hits
    const float presenceSeconds = std::max(presenceSlew, slew);
    levelPresence = glide(levelPresence, level, presenceSeconds, dt);
    subPresence = glide(subPresence, sub, presenceSeconds, dt);
    bassPresence = glide(bassPresence, bass, presenceSeconds, dt);
    midPresence = glide(midPresence, mid, presenceSeconds, dt);
    highPresence = glide(highPresence, high, presenceSeconds, dt);

    // a voice or a piano coming in: the mids over where they have sat for
    // the last several seconds. Up over a third of a second, out over two,
    // and back to nothing once the new level has become the room's
    midBaseline = glide(midBaseline, midPresence, 8.0f, dt);
    const float risen = std::clamp((midPresence - midBaseline) * 5.0f, 0.0f, 1.0f);
    midEntry = ease(midEntry, risen, 0.3f, 2.0f, dt);
    kick = read(AudioChannel::BassHits);
    onset = read(AudioChannel::Hits);
    // slower than the bands, so a hat between kicks tints rather than flickers
    const float bands = bass + high;
    timbre = glide(timbre, bands > 0.02f ? high / bands : 0.5f, 0.4f, dt);

    // the last tempo heard stands: a stale bpm reads as zero, and zero is
    // not a tempo, it is a sender that went quiet. It sets a speed only -
    // nothing waits on the sender's beat
    const float bpmLevel = bus->get(AudioChannel::Bpm, now);
    if (bpmLevel > 0.0f)
    {
        bpm = 50.0f + bpmLevel * 170.0f;
    }

    // a beat is the rising edge, with hysteresis so a spike that wobbles
    // across the line is one beat and not three
    // a kick: the host's kick trigger, the moment it hears one - or, from a
    // host without it, the rising edge of bass_hits, with hysteresis, which
    // is only sampled 30 times a second and can miss a kick on the tail of
    // the last. Whichever comes first; at least a sixth of a second after
    // the last kick, so a flam (or both of them for one kick) is one
    if (host.kick > 0.5f)
    {
        host.kick = 0.0f;
        host.kickAt = now;
    }
    bool kicked = false;
    if (host.kickAt > kickSeen)
    {
        kickSeen = host.kickAt;
        kicked = (now - host.kickAt) < 0.1;
    }
    if (!kickHigh && kick > 0.6f)
    {
        kickHigh = true;
        kicked = true;
    }
    else if (kickHigh && kick < 0.3f)
    {
        kickHigh = false;
    }

    sinceHit += dt;
    hitNow = false;
    if (kicked)
    {
        if (sinceHit >= 0.16f)
        {
            // a kick within a second and a half of the last is the same
            // groove, and its gap is how long the next one will take
            if (sinceHit < 1.5f)
            {
                ++streak;
                hitGap = glide(hitGap, std::clamp(sinceHit, 0.2f, 1.5f), 1.0f, 0.5f);
            }
            else
            {
                streak = 1;
            }
            sinceHit = 0.0f;
            ++hits;
            hitNow = true;
        }
    }
    if (sinceHit > 1.5f)
    {
        streak = 0;
    }

    // the beat has kicked in on the third kick in a row; up quickly once it
    // has, down slowly, so a fill that drops a kick does not drop the look
    const float grooveTarget = (streak >= 3) ? 1.0f : 0.0f;
    groove = glide(groove, grooveTarget, grooveTarget > groove ? 0.8f : 2.5f, dt);

    // the cut is taken by whichever look sees it first; the time is shared,
    // so both looks in a blend light it together
    if (host.cut > 0.5f)
    {
        host.cut = 0.0f;
        host.cutAt = now;
    }
    const float sinceCut = static_cast<float>(now - host.cutAt);
    cutGlow = (sinceCut >= 0.0f && sinceCut < cutSeconds) ? 1.0f - smooth(sinceCut / std::max(cutSeconds, 0.01f)) : 0.0f;
}

float Pattern_Osc_Look::hitPhase() const
{
    return std::clamp(sinceHit / std::max(hitGap, 0.05f), 0.0f, 1.0f);
}

float Pattern_Osc_Look::hitEnvelope(float attack, float release) const
{
    if (sinceHit >= 1000.0f)
    {
        return 0.0f;
    }
    const float rise = std::max(attack, 0.0f);
    if (sinceHit < rise)
    {
        return smooth(sinceHit / rise);
    }
    return std::exp(-(sinceHit - rise) / std::max(release, 0.01f));
}

float Pattern_Osc_Look::around(const eio::HSVStripNode* node)
{
    const float x = nodeCoord(node).x / static_cast<float>(scanner::kStageColumns);
    return x - std::floor(x);
}

float Pattern_Osc_Look::finish(float value) const
{
    const float screen = std::clamp(std::max(host.brightA, host.brightB), 0.0f, 1.0f);
    value *= 1.0f - std::clamp(follow, 0.0f, 1.0f) * (1.0f - screen);
    value += (1.0f - value) * std::clamp(cutLift, 0.0f, 1.0f) * cutGlow;
    return std::clamp(value, 0.0f, 1.0f);
}

float Pattern_Osc_Look::balance() const
{
    const float a = std::max(host.amountA, 0.0f);
    const float b = std::max(host.amountB, 0.0f);
    // kept off the ends, so neither colour leaves the stage entirely
    return (a + b) > 0.02f ? std::clamp(a / (a + b), 0.15f, 0.85f) : 0.5f;
}

void Pattern_Osc_Look::reflect(ecore::PropertyBag& bag)
{
    bag.add("hue_a", host.hueA, 0.0f, 360.0f);
    bag.add("sat_a", host.satA, 0.0f, 1.0f);
    bag.add("hue_b", host.hueB, 0.0f, 360.0f);
    bag.add("sat_b", host.satB, 0.0f, 1.0f);
    bag.add("bright_a", host.brightA, 0.0f, 1.0f);
    bag.add("bright_b", host.brightB, 0.0f, 1.0f);
    bag.add("amount_a", host.amountA, 0.0f, 1.0f);
    bag.add("amount_b", host.amountB, 0.0f, 1.0f);
    bag.add("tick", host.tick, 0.0f, 16000000.0f);
    bag.add("cut", host.cut, 0.0f, 1.0f);
    bag.add("kick", host.kick, 0.0f, 1.0f);
    bag.add("gain", gain, 0.0f, 2.0f);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
    bag.add("slew", slew, 0.0f, 1.0f);
    bag.add("follow", follow, 0.0f, 1.0f);
    bag.add("cut_lift", cutLift, 0.0f, 1.0f);
    bag.add("cut_seconds", cutSeconds, 0.05f, 4.0f);
    bag.add("calm", calm, 0.0f, 1.0f);
    bag.add("presence_slew", presenceSlew, 0.05f, 3.0f);
}

// ============================================================================
// osc_wash
// ============================================================================

void Pattern_Osc_Wash::reset()
{
    Pattern_Osc_Look::reset();
    roll = 0.0f;
    drift = 0.0f;
}

void Pattern_Osc_Wash::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    const float dt = std::max(deltaTime, 0.0f);
    // a tide, not a tempo: a stage in about forty seconds on a quiet room,
    // in ten with the mids up, and slower again before the beat
    const float pace = 0.5f + 0.5f * drive();
    roll = std::fmod(roll + dt * (0.025f + 0.075f * midPresence) * pace, 1.0f);
    drift = std::fmod(drift + dt * (0.03f + 0.09f * midPresence) * pace, 1.0f);
    time = std::fmod(time + dt, 3600.0f);
}

void Pattern_Osc_Wash::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    const float round = around(inNode);
    // a triangle wave up the stage, so the gradient rolls without a seam.
    // Its values are spread evenly over the stage, so moving the crossover
    // to the primary's share of the screen gives it that share of the stage
    const float along = std::fmod(height - roll + 1.0f, 1.0f);
    const float tri = 1.0f - std::fabs(along * 2.0f - 1.0f);
    const float mix = smooth((tri - balance()) * 2.0f + 0.5f);

    // the bass: the gradient's body, the whole stage
    const float body = 0.6f * bassPresence + 0.25f * levelPresence;
    // the sub: a swell filling up from the floor, as high as it is deep
    const float swellTop = 0.1f + std::max(swell, 0.0f) * subPresence;
    const float swellLayer = subPresence * (1.0f - smooth((height - swellTop) / 0.2f + 0.5f));
    // the mids: a slow wave of brightness drifting round the faces
    const float wave = 0.5f + 0.5f * std::sin(6.2831853f * (round + drift) + height * 4.0f);
    const float midLayer = 0.4f * midPresence * wave;
    // the highs: a soft shimmer, thickest at the top, crawling - not flashing
    const float sparkle = std::pow(0.5f + 0.5f * std::sin(height * 61.0f + round * 37.7f + time * 2.5f), 6.0f);
    const float highLayer = glitter * highPresence * sparkle * smooth((height - 0.3f) / 0.7f);

    const float music = std::clamp(body + 0.6f * swellLayer + midLayer, 0.0f, 1.0f) * drive();
    float value = floorLevel + (1.0f - floorLevel) * music;
    value += (1.0f - value) * highLayer * drive();

    // the swell deepens toward the primary, the shimmer lightens toward the
    // secondary, so each layer reads in its own colour
    ecore::HSV colour = blendRgb(primary(1.0f), secondary(1.0f), mix);
    colour = blendRgb(colour, primary(1.0f), std::clamp(swellLayer * 0.6f, 0.0f, 1.0f));
    colour = blendRgb(colour, secondary(1.0f), std::clamp(highLayer * 2.0f, 0.0f, 1.0f));
    inOutColor = colour;
    inOutColor.setBrightnessAlpha(finish(value));
}

void Pattern_Osc_Wash::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("swell", swell, 0.0f, 1.0f);
    bag.add("glitter", glitter, 0.0f, 1.0f);
}

// ============================================================================
// osc_bloom
// ============================================================================

void Pattern_Osc_Bloom::reset()
{
    Pattern_Osc_Look::reset();
    turn = 0.0f;
    climb = 0.0f;
}

void Pattern_Osc_Bloom::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    const float dt = std::max(deltaTime, 0.0f);
    // each layer on its band, eased: up quickly enough to feel it, down
    // slowly enough that it glides rather than flickers
    lows = ease(lows, std::max(bass, sub * 0.8f), attack, release, dt);
    mids = ease(mids, mid, attack, release, dt);
    highs = ease(highs, high, attack, release, dt);
    // the pars on the presence, slower: the room, not the hits; a voice
    // coming in lifts the mids
    parLows = ease(parLows, std::max(bassPresence, subPresence * 0.8f), 0.4f, 1.5f, dt);
    parMids = ease(parMids, std::clamp(midPresence + 0.5f * midEntry, 0.0f, 1.0f), 0.4f, 1.5f, dt);
    parHighs = ease(parHighs, highPresence, 0.4f, 1.5f, dt);
    const float pace = 0.5f + 0.5f * drive();
    turn = std::fmod(turn + dt * std::max(spin, 0.0f) * (0.25f + 0.75f * mids) * pace, 1.0f);
    climb = std::fmod(climb + dt * (0.12f + 0.45f * highs) * pace, 1.0f);
}

void Pattern_Osc_Bloom::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float power = drive();
    if (eio::nodeSpace(inNode) == eio::NodeSpace::Truss)
    {
        // the pars: the room in the host's two colours - the primary for the
        // lows, the secondary for the mids and highs, mixed by how much of
        // each is there, as bright as the strongest
        const float secondaryWeight = parMids + 0.5f * parHighs;
        // pushed off the middle, since two of the host's colours far apart
        // on the wheel mix to grey there
        const float even = secondaryWeight / std::max(parLows + secondaryWeight, 0.001f);
        const float share = smooth((even - 0.5f) * 3.0f + 0.5f);
        const float lit = std::max({parLows, parMids, parHighs}) * (0.35f + 0.65f * power);
        inOutColor = blendRgb(primary(1.0f), secondary(1.0f), std::clamp(share, 0.0f, 1.0f));
        inOutColor.setBrightnessAlpha(finish(floorLevel + (1.0f - floorLevel) * std::clamp(lit, 0.0f, 1.0f)));
        return;
    }

    const float height = stageAlpha(inNode);
    const float round = around(inNode);
    const float away = std::fabs(height - kOriginAlpha);

    // the lows, primary: the core, as far each way as the bass and only as
    // bright as it - no bass, no core - and the sub's dim halo over the
    // whole stage, heavier near the line
    const float coreReach = 0.04f + std::max(reach, 0.0f) * lows * power;
    const float coreOff = away / coreReach;
    const float core = std::exp(-coreOff * coreOff) * 0.85f * lows;
    const float halo = 0.3f * subPresence * (1.0f - 0.6f * std::min(away * 2.0f, 1.0f));

    // the mids, secondary: a shell just outside the core, in two lobes
    // turning round the faces, and a broad body through the middle of the
    // tower, so a voice fills it rather than tracing an outline
    const float shellAt = coreReach + 0.06f + 0.15f * mids;
    const float shellOff = (away - shellAt) / 0.14f;
    const float lobes = 0.5f + 0.5f * std::cos(6.2831853f * (round * 2.0f + turn));
    const float body = 0.35f * mids * std::exp(-std::pow((height - 0.55f) / 0.3f, 2.0f));
    const float shellLayer = shell * mids * std::exp(-shellOff * shellOff) * (0.45f + 0.55f * lobes) + body;

    // the highs, primary again over the secondary: a thread up (and down)
    // each column, leaving the core's edge and thinning out, each column on
    // its own stagger; and a secondary haze up the tower
    const float column = std::floor(round * static_cast<float>(scanner::kStageColumns));
    const float along = std::fmod(climb + column * 0.382f, 1.0f);
    const float threadOff = (away - (coreReach + along * 0.5f)) / 0.05f;
    const float thread = highs * std::exp(-threadOff * threadOff) * (1.0f - along);
    const float haze = 0.4f * highs * smooth((height - kOriginAlpha) / (1.0f - kOriginAlpha));

    // each layer in its colour, the two mixed by how much of each is lit
    const float primaryLit = core + halo + thread;
    const float secondaryLit = shellLayer + haze;
    const float lit = 1.0f - (1.0f - std::min(primaryLit, 1.0f)) * (1.0f - std::min(secondaryLit, 1.0f));
    const float share = secondaryLit / std::max(primaryLit + secondaryLit, 0.001f);

    const float value = floorLevel + (1.0f - floorLevel) * std::clamp(lit * power, 0.0f, 1.0f);
    inOutColor = blendRgb(primary(1.0f), secondary(1.0f), std::clamp(share, 0.0f, 1.0f));
    inOutColor.setBrightnessAlpha(finish(value));
}

void Pattern_Osc_Bloom::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("reach", reach, 0.0f, 1.0f);
    bag.add("shell", shell, 0.0f, 1.0f);
    bag.add("spin", spin, 0.0f, 1.0f);
    bag.add("attack", attack, 0.0f, 1.0f);
    bag.add("release", release, 0.02f, 3.0f);
}

// ============================================================================
// osc_meter
// ============================================================================

void Pattern_Osc_Meter::reset()
{
    Pattern_Osc_Look::reset();
    for (float& peak : peaks)
    {
        peak = 0.0f;
    }
}

void Pattern_Osc_Meter::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    const float dt = std::max(deltaTime, 0.0f);
    const float bars[4] = {subPresence, bassPresence, midPresence, highPresence};
    for (int band = 0; band < 4; ++band)
    {
        peaks[band] = std::max(bars[band], peaks[band] - std::max(fall, 0.0f) * dt);
    }
    // the pars on the presence, eased slowly, and all of them lifted a
    // little as a voice or a piano comes in
    const float lift = 0.35f * midEntry;
    const float bands[5] = {subPresence, bassPresence, midPresence, highPresence, levelPresence};
    for (int band = 0; band < 5; ++band)
    {
        pars[band] = ease(pars[band], std::clamp(bands[band] + lift, 0.0f, 1.0f), attack, release, dt);
    }
    // the wheel turns once a minute and a half
    hueDrift = std::fmod(hueDrift + dt * 4.0f, 360.0f);
}

void Pattern_Osc_Meter::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    // a couple of pixels' worth of stage, so a bar's head is soft
    constexpr float kEdge = 0.06f;
    const float strength = 0.45f + 0.55f * drive();
    const eio::NodeSpace space = eio::nodeSpace(inNode);

    if (space == eio::NodeSpace::Truss)
    {
        // the pars: mirrored out from the middle of the truss - sub, bass,
        // mid, high, then the level on the ends - a pair to a band
        const float along = std::clamp((nodeCoord(inNode).x - 2.5f) / 2.0f, 0.0f, 1.0f);
        const float out = std::fabs(along - 0.5f) * 2.0f;
        const int band = std::clamp(static_cast<int>(out * 4.5f), 0, 4);
        constexpr float kParHues[5] = {0.0f, 30.0f, 120.0f, 220.0f, 285.0f};
        const float lit = 0.2f + 0.8f * pars[band];
        inOutColor = ecore::HSV(std::fmod(kParHues[band] + hueDrift, 360.0f), 1.0f, 1.0f);
        inOutColor.setBrightnessAlpha(own(std::max(floorLevel, lit * strength)));
        return;
    }

    if (space == eio::NodeSpace::Ring)
    {
        // the ring: the whole level, lit round both ways from its foot, the
        // rainbow round it
        const Coordinate at = nodeCoord(inNode);
        const float angle = std::atan2(at.x - scanner::kRingCenterX, scanner::kRingCenterY - at.y);
        const float fromFoot = std::fabs(angle) / 3.14159265f;
        const float lit = 1.0f - smooth((fromFoot - levelPresence) / 0.08f + 0.5f);
        inOutColor = ecore::HSV(std::fmod(fromFoot * 300.0f + hueDrift, 360.0f), 1.0f, 1.0f);
        inOutColor.setBrightnessAlpha(own(std::max(floorLevel, lit * strength)));
        return;
    }

    // a face to a band: columns 0-1 the sub, 2-3 the bass, 4-5 the mids,
    // 6-7 the highs
    const float height = stageAlpha(inNode);
    const int band = std::clamp(static_cast<int>(around(inNode) * 4.0f), 0, 3);
    const float bars[4] = {subPresence, bassPresence, midPresence, highPresence};
    const float bar = bars[band];

    const float lit = 1.0f - smooth((height - bar) / kEdge + 0.5f);
    const float cap = std::exp(-std::pow((height - peaks[band]) / kEdge, 2.0f));

    // the rainbow up the bar, each face a quarter of the wheel on; the cap
    // pale
    const float hue = std::fmod(height * 300.0f + static_cast<float>(band) * 90.0f + hueDrift, 360.0f);
    const float sat = 0.9f - 0.6f * cap * (1.0f - lit);

    const float value = std::max({floorLevel * (1.0f - lit), lit * strength, cap * 0.45f * strength});
    inOutColor = ecore::HSV(hue, sat, 1.0f);
    inOutColor.setBrightnessAlpha(own(value));
}

void Pattern_Osc_Meter::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("fall", fall, 0.0f, 2.0f);
    bag.add("attack", attack, 0.0f, 1.0f);
    bag.add("release", release, 0.02f, 3.0f);
}

// ============================================================================
// osc_chase
// ============================================================================

void Pattern_Osc_Chase::reset()
{
    Pattern_Osc_Look::reset();
    scroll = 0.0f;
    for (float& age : pulseAges)
    {
        age = 1000.0f;
    }
}

void Pattern_Osc_Chase::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    // a stripe past any one point every `beats_per_stripe` beats, on the
    // sender's tempo - a speed, run free, so a beat that lands early or late
    // never jumps it
    const float stripesPerSecond = bpm / 60.0f / std::max(beatsPerStripe, 0.25f);
    scroll = std::fmod(scroll + std::max(deltaTime, 0.0f) * stripesPerSecond, 1.0f);

    // each kick starts a pulse at the foot; they climb on their own clocks,
    // so a run of kicks is a run of swells rather than one restarting
    for (float& age : pulseAges)
    {
        age = std::min(age + std::max(deltaTime, 0.0f), 1000.0f);
    }
    if (hitNow)
    {
        pulseAges[nextPulse] = 0.0f;
        nextPulse = (nextPulse + 1) % kPulses;
    }
}

void Pattern_Osc_Chase::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    const float round = around(inNode);
    const float power = drive();

    // the pole: a few stripes up the stage, wound `turns` times round the
    // faces, climbing; every other stripe primary, the rest secondary, a wide
    // dark gap between - the marquee's lamps, fattening as a voice comes in
    const float count = std::max(std::round(stripes), 1.0f);
    const float wound = height * count + round * std::round(turns) * 2.0f - scroll * 2.0f;
    const float cell = wound - 2.0f * std::floor(wound * 0.5f);          // 0..2: two stripes, one of each
    const float within = cell - std::floor(cell);                         // 0..1 across a stripe
    const float lampWidth = std::clamp(0.22f + 0.25f * midEntry, 0.05f, 0.9f);
    const float edge = 0.1f;
    const float lamp = smooth((within - (0.5f - lampWidth * 0.5f)) / edge + 0.5f)
                     * (1.0f - smooth((within - (0.5f + lampWidth * 0.5f)) / edge + 0.5f));
    const bool isPrimary = cell < 1.0f;

    // the kicks' pulses: soft fronts climbing the pole from the foot over
    // `climb` seconds each, swelling in over the attack and fading over the
    // release as they go, overlapping into a swell on a run of kicks
    float pulseSum = 0.0f;
    for (const float age : pulseAges)
    {
        if (age >= 1000.0f)
        {
            continue;
        }
        const float front = smooth(std::min(age / std::max(climb, 0.2f), 1.0f)) * 1.2f - 0.1f;
        const float frontOff = (height - front) / 0.1f;
        const float swellIn = (attack > 0.0f && age < attack) ? smooth(age / attack) : 1.0f;
        const float fadeOut = std::exp(-std::max(age - attack, 0.0f) / std::max(release, 0.01f));
        pulseSum += std::exp(-frontOff * frontOff) * swellIn * fadeOut;
    }
    const float pulseLayer = std::clamp(pulse, 0.0f, 1.0f) * std::min(pulseSum, 1.0f) * power;

    // the lamps' own glow: low, up with the level, filled in by a voice
    const float glow = (0.12f + 0.3f * levelPresence + 0.35f * midEntry) * power;
    const float lit = lamp * std::clamp(glow + pulseLayer, 0.0f, 1.0f);

    // the sub and bass a glow at the foot, between the lamps
    const float foot = 1.0f - height;
    const float bed = (0.4f * subPresence * foot * foot + 0.25f * bassPresence * foot) * power;

    float value = floorLevel + (1.0f - floorLevel) * std::clamp(bed, 0.0f, 1.0f);
    value += (1.0f - value) * lit;

    ecore::HSV colour = blendRgb(primary(1.0f), secondary(1.0f), balance());
    colour = blendRgb(colour, isPrimary ? primary(1.0f) : secondary(1.0f), std::clamp(lamp, 0.0f, 1.0f));
    inOutColor = colour;
    inOutColor.setBrightnessAlpha(finish(value));
}

void Pattern_Osc_Chase::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("stripes", stripes, 1.0f, 16.0f);
    bag.add("turns", turns, 0.0f, 4.0f);
    bag.add("beats_per_stripe", beatsPerStripe, 0.25f, 8.0f);
    bag.add("pulse", pulse, 0.0f, 1.0f);
    bag.add("climb", climb, 0.2f, 10.0f);
    bag.add("attack", attack, 0.0f, 0.5f);
    bag.add("release", release, 0.02f, 2.0f);
}

// ============================================================================
// osc_steps
// ============================================================================

void Pattern_Osc_Steps::reset()
{
    Pattern_Osc_Look::reset();
    time = 0.0f;
    // the first kick lands on the foot
    step = 3;
}

void Pattern_Osc_Steps::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    time = std::fmod(time + std::max(deltaTime, 0.0f), 3600.0f);
    // the wheel turns once every minute and a half
    hueDrift = std::fmod(hueDrift + std::max(deltaTime, 0.0f) * 4.0f, 360.0f);
    if (hitNow)
    {
        step = (step + 1) % 4;
    }
}

void Pattern_Osc_Steps::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    const float round = around(inNode);
    const int quarter = std::clamp(static_cast<int>(height * 4.0f), 0, 3);
    const float power = drive();
    const float swell = hitEnvelope(attack, release);

    // the sub, under everything; the level as a calm glow
    const float glow = (0.25f * subPresence + 0.2f * levelPresence) * power;

    // a voice or a piano coming in: a glow rising up the stage from the foot
    const float entryTop = 0.15f + 0.75f * midEntry;
    const float entry = 0.5f * midEntry * (1.0f - smooth((height - entryTop) / 0.25f + 0.5f)) * power;

    // this kick's step, in the colour opposite the last one's, swelling in
    // and easing out; soft at the quarter's edges so it is a shape and not a
    // block; the last step lingering on the mids
    const float inQuarter = height * 4.0f - static_cast<float>(quarter);
    const float shape = 0.6f + 0.4f * std::sin(3.14159265f * inQuarter);
    float lit = 0.0f;
    // the step whose colour this quarter wears: this kick's, or the last
    int litStep = step;
    if (quarter == step)
    {
        lit = (0.5f + 0.3f * bassPresence) * swell * shape * power;
        // the highs glint on the lit step
        const float glint = std::pow(0.5f + 0.5f * std::sin(height * 83.0f + round * 41.0f + time * 9.0f), 8.0f);
        lit += (1.0f - lit) * 0.6f * highPresence * glint * swell * power;
    }
    else if (quarter == (step + 3) % 4)
    {
        lit = 0.45f * midPresence * (1.0f - hitPhase()) * shape * power;
        litStep = (step + 3) % 4;
    }

    // every kick breathes the whole stage a little, the foot's step most
    const float breath = std::clamp(flash, 0.0f, 1.0f) * (step == 0 ? 1.6f : 1.0f) * swell * power;

    float value = floorLevel + (1.0f - floorLevel) * std::clamp(glow + entry, 0.0f, 1.0f);
    value += (1.0f - value) * lit;
    value += (1.0f - value) * std::clamp(breath, 0.0f, 1.0f);

    // the ground a dim rainbow up the stage, drifting - never flipping with
    // the step - and each step a quarter of the wheel on from the last
    const ecore::HSV ground(std::fmod(hueDrift + height * 120.0f, 360.0f), 0.85f, 1.0f);
    const ecore::HSV stepColour(std::fmod(hueDrift + 180.0f + static_cast<float>(litStep) * 90.0f, 360.0f), 1.0f, 1.0f);
    ecore::HSV colour = ground;
    colour = blendRgb(colour, stepColour, std::clamp(lit * 1.5f, 0.0f, 1.0f));
    inOutColor = colour;
    inOutColor.setBrightnessAlpha(own(value));
}

void Pattern_Osc_Steps::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("flash", flash, 0.0f, 1.0f);
    bag.add("attack", attack, 0.0f, 0.5f);
    bag.add("release", release, 0.02f, 2.0f);
}

// ============================================================================
// osc_spectrum
// ============================================================================

void Pattern_Osc_Spectrum::reset()
{
    Pattern_Osc_Look::reset();
}

void Pattern_Osc_Spectrum::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    const float dt = std::max(deltaTime, 0.0f);
    // the wheel turns once every minute and a half
    hueDrift = std::fmod(hueDrift + dt * 4.0f, 360.0f);
    // red, green and blue on the lows, mids and highs: quick, but eased both
    // ways so a hit is felt without the light snapping
    red = ease(red, std::max(bass, sub * 0.8f), attack, release, dt);
    green = ease(green, mid, attack, release, dt);
    blue = ease(blue, high, attack, release * 0.8f, dt);
}

void Pattern_Osc_Spectrum::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float power = drive();
    const float base = floorLevel * 0.5f;

    if (eio::nodeSpace(inNode) == eio::NodeSpace::Truss)
    {
        // the pars: the three bands as light, straight
        const Rgb light{base + (1.0f - base) * std::clamp(red * power, 0.0f, 1.0f),
                        base + (1.0f - base) * std::clamp(green * power, 0.0f, 1.0f),
                        base + (1.0f - base) * std::clamp(blue * power, 0.0f, 1.0f)};
        const float value = std::max({light.r, light.g, light.b, 0.0001f});
        inOutColor = toHsv({light.r / value, light.g / value, light.b / value});
        inOutColor.setBrightnessAlpha(own(value));
        return;
    }

    const float height = stageAlpha(inNode);

    // the bands as light, each its own part of the stage, mixing where they
    // meet: red the lows, filling up from the foot as far as they reach;
    // green the mids, a band through the middle widening with them; blue
    // the highs, coming down from the top
    const float redLayer = red * (1.0f - smooth((height - (0.15f + 0.6f * red)) / 0.15f + 0.5f));
    const float greenOff = (height - 0.5f) / (0.08f + 0.3f * green);
    const float greenLayer = green * std::exp(-greenOff * greenOff);
    const float blueLayer = blue * smooth((height - (0.85f - 0.6f * blue)) / 0.15f + 0.5f);

    // the sub and the level as a dim rainbow ground up the stage, drifting;
    // a voice or a piano coming in rising up it from the foot
    const float entryTop = 0.15f + 0.75f * midEntry;
    const float entry = 0.5f * midEntry * (1.0f - smooth((height - entryTop) / 0.25f + 0.5f));
    const float groundLevel = 0.2f * subPresence + 0.15f * levelPresence + entry;
    const Rgb ground = toRgb(ecore::HSV(std::fmod(hueDrift + height * 120.0f, 360.0f), 0.85f, 1.0f));

    // a kick breathes the whole stage a little
    const float breath = std::clamp(flash, 0.0f, 1.0f) * hitEnvelope(kickAttack, kickRelease);

    // all of it as light: added, then held under full by its brightest
    Rgb light;
    light.r = base + (1.0f - base) * std::clamp((ground.r * groundLevel + redLayer + breath) * power, 0.0f, 1.0f);
    light.g = base + (1.0f - base) * std::clamp((ground.g * groundLevel + greenLayer + breath) * power, 0.0f, 1.0f);
    light.b = base + (1.0f - base) * std::clamp((ground.b * groundLevel + blueLayer + breath) * power, 0.0f, 1.0f);
    const float value = std::max({light.r, light.g, light.b, 0.0001f});
    inOutColor = toHsv({light.r / value, light.g / value, light.b / value});
    inOutColor.setBrightnessAlpha(own(value));
}

void Pattern_Osc_Spectrum::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("flash", flash, 0.0f, 1.0f);
    bag.add("attack", attack, 0.0f, 0.5f);
    bag.add("release", release, 0.02f, 2.0f);
    bag.add("kick_attack", kickAttack, 0.0f, 0.5f);
    bag.add("kick_release", kickRelease, 0.02f, 2.0f);
}
