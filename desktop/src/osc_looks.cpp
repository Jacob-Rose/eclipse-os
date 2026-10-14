// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/osc_looks.h"

#include <algorithm>
#include <cmath>

#include "edmx/color_mix.h"

using namespace edmx;

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

OscHost& edmx::sharedOscHost()
{
    static OscHost host;
    return host;
}

void Pattern_Osc_Look::reset()
{
    PatternScanner::reset();
    // the levels are not reset: they are the sender's, and a look coming up
    // mid-song should come up at the song's level, not climb to it
    sinceBeat = 1000.0f;
    beats = 0;
    // the tick standing in the knob is the last one heard, not a new beat
    lastTick = host.tick;
    sinceTick = 1000.0f;
    downbeat = false;
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
    kick = read(AudioChannel::BassHits);
    onset = read(AudioChannel::Hits);
    // slower than the bands, so a hat between kicks tints rather than flickers
    const float bands = bass + high;
    timbre = glide(timbre, bands > 0.02f ? high / bands : 0.5f, 0.4f, dt);

    // the last tempo heard stands: a stale bpm reads as zero, and zero is
    // not a tempo, it is a sender that went quiet
    const float bpmLevel = bus->get(AudioChannel::Bpm, now);
    if (bpmLevel > 0.0f)
    {
        bpm = 50.0f + bpmLevel * 170.0f;
    }

    // a beat is the rising edge, with hysteresis so a spike that wobbles
    // across the line is one beat and not three
    sinceBeat += dt;
    const float period = 60.0f / std::max(bpm, 1.0f);
    // a beat counts toward the groove when it comes within reach of the last
    // one; the sender itself gives up on a beat after about two seconds of
    // none, and so does the streak
    const float reachSeconds = std::max(2.5f * period, 2.0f);
    const float beat = bus->get(AudioChannel::Beat, now);
    if (!beatHigh && beat > 0.5f)
    {
        beatHigh = true;
        streak = (sinceBeat < reachSeconds) ? streak + 1 : 1;
        sinceBeat = 0.0f;
        ++beats;
    }
    else if (beatHigh && beat < 0.25f)
    {
        beatHigh = false;
    }
    if (sinceBeat > reachSeconds)
    {
        streak = 0;
    }

    // the beat has kicked in on the third in a row; up quickly once it has,
    // down slowly, so a fill that drops a beat does not drop the look
    const float grooveTarget = (streak >= 3) ? 1.0f : 0.0f;
    groove = glide(groove, grooveTarget, grooveTarget > groove ? 0.8f : 2.5f, dt);

    // the host's tick says which beat this is: the one when its count is a
    // multiple of four. It lands within a frame of the beat's edge, so the
    // accent it sets is on the ring that beat starts. A host that stops
    // ticking (it lost the beat) leaves every beat the same.
    sinceTick += dt;
    if (host.tick != lastTick)
    {
        lastTick = host.tick;
        sinceTick = 0.0f;
        downbeat = (static_cast<long>(std::lround(host.tick)) % 4) == 0;
    }
    if (sinceTick > 3.0f)
    {
        downbeat = false;
    }

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

float Pattern_Osc_Look::beatPhase() const
{
    const float period = 60.0f / std::max(bpm, 1.0f);
    return std::clamp(sinceBeat / period, 0.0f, 1.0f);
}

float Pattern_Osc_Look::accent() const
{
    // with the tick arriving, the one is full and the rest sit under it;
    // without, every beat is full
    return (sinceTick > 3.0f || downbeat) ? 1.0f : 0.7f;
}

int Pattern_Osc_Look::barBeat() const
{
    if (sinceTick <= 3.0f)
    {
        const long n = static_cast<long>(std::lround(host.tick));
        return static_cast<int>(((n % 4) + 4) % 4);
    }
    // no tick: the beats since entry, the first of them the one
    return ((std::max(beats, 1) - 1) % 4);
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
    const float pace = 0.5f + 0.5f * drive();
    turn = std::fmod(turn + dt * std::max(spin, 0.0f) * (0.25f + 0.75f * midPresence) * pace, 1.0f);
    climb = std::fmod(climb + dt * (0.12f + 0.45f * highPresence) * pace, 1.0f);
}

void Pattern_Osc_Bloom::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    const float round = around(inNode);
    const float away = std::fabs(height - kOriginAlpha);

    // the bass: the core, primary, as far each way as the bass is present
    const float coreReach = 0.04f + std::max(reach, 0.0f) * bassPresence * drive();
    const float coreOff = away / coreReach;
    const float core = std::exp(-coreOff * coreOff) * (0.3f + 0.7f * bassPresence);

    // the mids: a shell of secondary just outside it, in two lobes turning
    // round the faces
    const float shellAt = coreReach + 0.06f + 0.15f * midPresence;
    const float shellOff = (away - shellAt) / 0.08f;
    const float lobes = 0.5f + 0.5f * std::cos(6.2831853f * (round * 2.0f + turn));
    const float shellLayer = shell * midPresence * std::exp(-shellOff * shellOff) * (0.35f + 0.65f * lobes);

    // the highs: a thread up (and down) each column, leaving the core's edge
    // and thinning out as it goes; each column on its own stagger
    const float column = std::floor(round * static_cast<float>(scanner::kStageColumns));
    const float along = std::fmod(climb + column * 0.382f, 1.0f);
    const float threadOff = (away - (coreReach + along * 0.5f)) / 0.03f;
    const float thread = highPresence * std::exp(-threadOff * threadOff) * (1.0f - along);

    // the sub: a dim halo over the whole stage, heavier near the line
    const float halo = 0.5f * subPresence * (1.0f - 0.6f * std::min(away * 2.0f, 1.0f));

    const float lit = 1.0f - (1.0f - core) * (1.0f - shellLayer) * (1.0f - thread) * (1.0f - halo);
    const float value = floorLevel + (1.0f - floorLevel) * std::clamp(lit, 0.0f, 1.0f) * drive();

    // each layer its colour: the core and halo primary, the shell and the
    // threads secondary
    const float secondaryShare = (shellLayer + thread) / std::max(core + halo + shellLayer + thread, 0.001f);
    inOutColor = blendRgb(primary(1.0f), secondary(1.0f), std::clamp(secondaryShare, 0.0f, 1.0f));
    inOutColor.setBrightnessAlpha(finish(value));
}

void Pattern_Osc_Bloom::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("reach", reach, 0.0f, 1.0f);
    bag.add("shell", shell, 0.0f, 1.0f);
    bag.add("spin", spin, 0.0f, 1.0f);
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
    const float bars[4] = {subPresence, bassPresence, midPresence, highPresence};
    for (int band = 0; band < 4; ++band)
    {
        peaks[band] = std::max(bars[band], peaks[band] - std::max(fall, 0.0f) * std::max(deltaTime, 0.0f));
    }
}

void Pattern_Osc_Meter::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    // a pixel's worth of stage, so a bar's head is soft rather than a step
    constexpr float kEdge = 0.03f;
    const float strength = 0.45f + 0.55f * drive();

    if (eio::nodeSpace(inNode) == eio::NodeSpace::Ring)
    {
        // the ring: the whole level, lit round both ways from its foot
        const Coordinate at = nodeCoord(inNode);
        const float angle = std::atan2(at.x - scanner::kRingCenterX, scanner::kRingCenterY - at.y);
        const float fromFoot = std::fabs(angle) / 3.14159265f;
        const float lit = 1.0f - smooth((fromFoot - levelPresence) / 0.08f + 0.5f);
        inOutColor = blendRgb(primary(1.0f), secondary(1.0f), fromFoot);
        inOutColor.setBrightnessAlpha(finish(std::max(floorLevel, lit * strength)));
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

    // the sub's face all primary, the highs' all secondary, the two between
    // stepping across; each bar's head a little further toward the next
    const float own = static_cast<float>(band) / 3.0f;
    const float head = bar > 0.0f ? std::clamp(height / bar, 0.0f, 1.0f) * 0.2f : 0.0f;
    ecore::HSV colour = blendRgb(primary(1.0f), secondary(1.0f), std::clamp(own + head, 0.0f, 1.0f));
    colour = blendRgb(colour, blendRgb(secondary(1.0f), primary(1.0f), own), cap * (1.0f - lit));

    const float value = std::max({floorLevel * (1.0f - lit), lit * strength, cap * 0.6f * strength});
    inOutColor = colour;
    inOutColor.setBrightnessAlpha(finish(value));
}

void Pattern_Osc_Meter::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("fall", fall, 0.0f, 2.0f);
}

// ============================================================================
// osc_chase
// ============================================================================

void Pattern_Osc_Chase::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    const float round = around(inNode);
    const float phase = beatPhase();
    const float period = 60.0f / std::max(bpm, 1.0f);

    // the bed: the sub and the bass, heaviest at the foot
    const float foot = 1.0f - height;
    const float bed = (0.7f * subPresence * foot * foot + 0.5f * bassPresence * foot + 0.25f * levelPresence) * drive();

    // the band: leaves the origin line on the beat and reaches the top as it
    // runs out, a helix round the faces, the other way on alternate beats
    const float turnWay = (beats % 2 == 0) ? 1.0f : -1.0f;
    const float centre = kOriginAlpha + phase * (1.0f - kOriginAlpha)
                       + 0.06f * std::sin(6.2831853f * (round + turnWay * twist * phase));
    const float bandOff = (height - centre) / std::max(width, 0.02f);
    const float fade = 1.0f - smooth(phase);
    const float band = std::exp(-bandOff * bandOff) * fade * accent() * (0.75f + 0.25f * kick) * groove;

    // the highs: a trail under the band, back toward the line
    const float behind = centre - height;
    const float trail = (behind > 0.0f && height > kOriginAlpha)
        ? highPresence * 0.6f * std::exp(-behind / 0.15f) * fade * groove : 0.0f;

    // the flash, as the band leaves: the whole stage, hardest on the one
    const float flashLayer = std::clamp(flash, 0.0f, 1.0f) * accent()
                           * std::exp(-sinceBeat / std::max(0.12f * period, 0.02f)) * groove;

    float value = floorLevel + (1.0f - floorLevel) * std::clamp(bed + trail, 0.0f, 1.0f);
    value += (1.0f - value) * band;
    value += (1.0f - value) * flashLayer;

    ecore::HSV colour = blendRgb(primary(1.0f), secondary(1.0f), balance());
    colour = blendRgb(colour, secondary(1.0f), std::clamp(trail * 2.0f, 0.0f, 1.0f));
    colour = blendRgb(colour, primary(1.0f), std::clamp(band, 0.0f, 1.0f));
    inOutColor = colour;
    inOutColor.setBrightnessAlpha(finish(value));
}

void Pattern_Osc_Chase::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("flash", flash, 0.0f, 1.0f);
    bag.add("width", width, 0.02f, 0.5f);
    bag.add("twist", twist, 0.0f, 2.0f);
}

// ============================================================================
// osc_steps
// ============================================================================

void Pattern_Osc_Steps::reset()
{
    Pattern_Osc_Look::reset();
    time = 0.0f;
}

void Pattern_Osc_Steps::tick(float deltaTime)
{
    Pattern_Osc_Look::tick(deltaTime);
    time = std::fmod(time + std::max(deltaTime, 0.0f), 3600.0f);
}

void Pattern_Osc_Steps::render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const
{
    const float height = stageAlpha(inNode);
    const float round = around(inNode);
    const float phase = beatPhase();
    const float period = 60.0f / std::max(bpm, 1.0f);
    const int step = barBeat();
    const int quarter = std::clamp(static_cast<int>(height * 4.0f), 0, 3);

    // the sub, under everything; the level as a calm glow before the beat
    const float glow = (0.45f * subPresence + 0.35f * levelPresence) * drive();

    // this beat's step, in the colour opposite the last one's, fading over
    // `decay` of the beat; the last step lingering on the mids
    const float fade = 1.0f - smooth(phase / std::max(decay, 0.05f));
    float lit = 0.0f;
    bool litPrimary = (step % 2) == 0;
    if (quarter == step)
    {
        lit = (0.55f + 0.45f * bassPresence) * fade * accent() * groove;
        // the highs glint on the lit step
        const float glint = std::pow(0.5f + 0.5f * std::sin(height * 83.0f + round * 41.0f + time * 9.0f), 8.0f);
        lit += (1.0f - lit) * highPresence * glint * fade * groove;
    }
    else if (quarter == (step + 3) % 4)
    {
        lit = 0.5f * midPresence * (1.0f - phase) * groove;
        litPrimary = !litPrimary;
    }

    // the one flashes the whole stage
    const bool one = (step == 0);
    const float flashLayer = one ? std::clamp(flash, 0.0f, 1.0f)
                                 * std::exp(-sinceBeat / std::max(0.1f * period, 0.02f)) * groove : 0.0f;

    float value = floorLevel + (1.0f - floorLevel) * std::clamp(glow, 0.0f, 1.0f);
    value += (1.0f - value) * lit;
    value += (1.0f - value) * flashLayer;

    const ecore::HSV stepColour = litPrimary ? primary(1.0f) : secondary(1.0f);
    ecore::HSV colour = blendRgb(primary(1.0f), secondary(1.0f), balance());
    colour = blendRgb(colour, stepColour, std::clamp(lit * 1.5f, 0.0f, 1.0f));
    inOutColor = colour;
    inOutColor.setBrightnessAlpha(finish(value));
}

void Pattern_Osc_Steps::reflect(ecore::PropertyBag& bag)
{
    Pattern_Osc_Look::reflect(bag);
    bag.add("flash", flash, 0.0f, 1.0f);
    bag.add("decay", decay, 0.05f, 1.0f);
}
