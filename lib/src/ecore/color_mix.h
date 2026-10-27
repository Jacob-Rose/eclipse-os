// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <algorithm>
#include <cmath>

#include "hsv.h"

namespace ecore
{
    /* @brief A colour as three 0..1 floats, for mixing the way a lamp does.
    *
    * HSV::blend is the fixed-point blend a relic renders with per pixel.
    * These are the float ones a desk (or a look with the budget) uses to
    * cross-fade whole frames: as light (RGB, what two lamps on one surface
    * make), as paint (HSV, with the hue taking the short way round), or
    * added. Purple into white through RGB stays purple; through a numeric
    * hue lerp it would pass through green.
    */
    struct Rgb
    {
        float r{0.0f};
        float g{0.0f};
        float b{0.0f};
    };

    inline Rgb toRgb(const HSV& hsv)
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

    inline HSV toHsv(const Rgb& rgb)
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

    /// a towards b as light: the two lamps' colours on one surface.
    inline HSV blendRgb(const HSV& a, const HSV& b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const Rgb ra = toRgb(a);
        const Rgb rb = toRgb(b);
        HSV out = toHsv({ra.r + (rb.r - ra.r) * t,
                         ra.g + (rb.g - ra.g) * t,
                         ra.b + (rb.b - ra.b) * t});
        out.setBrightnessAlpha(a.getValFloat() + (b.getValFloat() - a.getValFloat()) * t);
        return out;
    }

    /// The short way round the wheel, in degrees.
    inline float blendHue(float a, float b, float t)
    {
        float delta = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
        return std::fmod(a + delta * t + 720.0f, 360.0f);
    }

    /// a towards b as paint: hue the short way round, and a grey takes the
    /// other side's hue rather than pulling it through nothing.
    inline HSV blendHsv(const HSV& a, const HSV& b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const float satA = a.getSatFloat();
        const float satB = b.getSatFloat();
        float hue;
        if (satA <= 0.001f)      hue = b.getHueFloat();
        else if (satB <= 0.001f) hue = a.getHueFloat();
        else                     hue = blendHue(a.getHueFloat(), b.getHueFloat(), t);
        const float val = a.getValFloat() + (b.getValFloat() - a.getValFloat()) * t;
        HSV out(hue, satA + (satB - satA) * t, val);
        out.setBrightnessAlpha(val);
        return out;
    }

    /// over added to base as light, clipped at white.
    inline HSV addRgb(const HSV& base, const HSV& over)
    {
        const Rgb a = toRgb(base);
        const Rgb b = toRgb(over);
        return toHsv({std::min(a.r + b.r, 1.0f),
                      std::min(a.g + b.g, 1.0f),
                      std::min(a.b + b.b, 1.0f)});
    }
}
