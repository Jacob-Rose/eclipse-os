// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_blend.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace ecore;
using namespace esm;

namespace
{
    // ------------------------------------------------------------------
    // cut - there is no in-between
    // ------------------------------------------------------------------
    class Blend_Cut : public StateBlend
    {
    public:
        const char* getName() const override { return "cut"; }
        bool isInstant() const override { return true; }
        HSV mix(const HSV&, const HSV& to, float, float) const override { return to; }
    };

    // ------------------------------------------------------------------
    // crossfade - the straight lerp the relics have always done
    // ------------------------------------------------------------------
    class Blend_Crossfade : public StateBlend
    {
    public:
        const char* getName() const override { return "crossfade"; }
        HSV mix(const HSV& from, const HSV& to, float alpha, float) const override
        {
            return HSV::blend(from, to, alpha);
        }
    };

    // ------------------------------------------------------------------
    // rgb - two colours the way two lamps make one
    //
    // HSV::blend walks the hue the long way round the wheel, so orange into
    // sky blue passes through yellow and green on the way. A cross-fade on a
    // truss is two lamps dimming against each other, and that mixes in RGB.
    // The level is lerped on its own and put back over the mix: two saturated
    // colours meet in RGB at half the level of either, and a lamp changing
    // colour should not go dark in the middle.
    // ------------------------------------------------------------------
    struct Rgb
    {
        float r{0.0f};
        float g{0.0f};
        float b{0.0f};
    };

    Rgb toRgb(const HSV& hsv)
    {
        const float h = hsv.getHueFloat();
        const float s = hsv.getSatFloat();
        const float v = hsv.getValFloat();

        const float chroma = v * s;
        const float sector = std::fmod(h < 0.0f ? h + 360.0f : h, 360.0f) / 60.0f;
        const float x = chroma * (1.0f - std::fabs(std::fmod(sector, 2.0f) - 1.0f));
        const float m = v - chroma;

        Rgb out;
        switch (static_cast<int>(sector))
        {
            case 0:  out = {chroma, x, 0.0f};   break;
            case 1:  out = {x, chroma, 0.0f};   break;
            case 2:  out = {0.0f, chroma, x};   break;
            case 3:  out = {0.0f, x, chroma};   break;
            case 4:  out = {x, 0.0f, chroma};   break;
            default: out = {chroma, 0.0f, x};   break;
        }
        out.r += m;
        out.g += m;
        out.b += m;
        return out;
    }

    HSV toHsv(const Rgb& rgb)
    {
        const float maxC = std::max(rgb.r, std::max(rgb.g, rgb.b));
        const float minC = std::min(rgb.r, std::min(rgb.g, rgb.b));
        const float delta = maxC - minC;

        float hue = 0.0f;
        if (delta > 0.00001f)
        {
            if (maxC == rgb.r)      hue = 60.0f * std::fmod((rgb.g - rgb.b) / delta, 6.0f);
            else if (maxC == rgb.g) hue = 60.0f * (((rgb.b - rgb.r) / delta) + 2.0f);
            else                    hue = 60.0f * (((rgb.r - rgb.g) / delta) + 4.0f);
        }
        if (hue < 0.0f)
        {
            hue += 360.0f;
        }
        const float sat = (maxC <= 0.00001f) ? 0.0f : (delta / maxC);
        return HSV(hue, sat, maxC);
    }

    class Blend_Rgb : public StateBlend
    {
    public:
        const char* getName() const override { return "rgb"; }
        HSV mix(const HSV& from, const HSV& to, float alpha, float) const override
        {
            const Rgb a = toRgb(from);
            const Rgb b = toRgb(to);
            HSV out = toHsv({a.r + (b.r - a.r) * alpha,
                             a.g + (b.g - a.g) * alpha,
                             a.b + (b.b - a.b) * alpha});
            out.setBrightnessAlpha(from.getValFloat() + (to.getValFloat() - from.getValFloat()) * alpha);
            return out;
        }
    };

    // ------------------------------------------------------------------
    // dip - through black
    //
    // The outgoing look dims to nothing over the first half and the incoming
    // one rises from it over the second, so the two never share a frame. This
    // is what a cue change between two looks that should not mix wants - a
    // fire into a deep blue reads as a fire going out and a blue coming up,
    // not as a purple.
    // ------------------------------------------------------------------
    class Blend_Dip : public StateBlend
    {
    public:
        const char* getName() const override { return "dip"; }
        HSV mix(const HSV& from, const HSV& to, float alpha, float) const override
        {
            HSV out = (alpha < 0.5f) ? from : to;
            const float level = (alpha < 0.5f) ? (1.0f - alpha * 2.0f) : (alpha * 2.0f - 1.0f);
            out.setBrightnessAlpha(out.getValFloat() * level);
            return out;
        }
    };

    // ------------------------------------------------------------------
    // wipe - the new look arrives along the strip
    //
    // Each node runs its own crossfade, started later the further along the
    // strip it sits, with a soft edge a third of the strip wide so the front
    // reads as a sweep rather than a stepping row of lamps. The first node is
    // fully across when alpha reaches the edge width; the last as alpha
    // reaches 1.
    // ------------------------------------------------------------------
    class Blend_Wipe : public StateBlend
    {
    public:
        const char* getName() const override { return "wipe"; }
        HSV mix(const HSV& from, const HSV& to, float alpha, float position) const override
        {
            constexpr float edge = 0.35f;
            const float local = std::clamp((alpha * (1.0f + edge) - position) / edge, 0.0f, 1.0f);
            return HSV::blend(from, to, local);
        }
    };

    const Blend_Cut       kCut;
    const Blend_Crossfade kCrossfade;
    const Blend_Rgb       kRgb;
    const Blend_Dip       kDip;
    const Blend_Wipe      kWipe;

    // Order is the order a UI shows them in.
    const StateBlend* const kBlends[] = { &kCut, &kCrossfade, &kRgb, &kDip, &kWipe, nullptr };

    const char* const kNames[] = { "cut", "crossfade", "rgb", "dip", "wipe", nullptr };
}

const StateBlend* esm::findStateBlend(const char* name)
{
    if (name == nullptr)
    {
        return nullptr;
    }
    for (const StateBlend* const* blend = kBlends; *blend != nullptr; ++blend)
    {
        if (std::strcmp((*blend)->getName(), name) == 0)
        {
            return *blend;
        }
    }
    return nullptr;
}

const StateBlend& esm::defaultStateBlend()
{
    return kCrossfade;
}

const char* const* esm::stateBlendNames()
{
    return kNames;
}
