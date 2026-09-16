// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <algorithm>
#include <cmath>

#include "lib/ecore/hsv.h"

///
/// Colour mixing for the desktop, in float.
///
/// ecore::HSV::blend lerps the hue as a number, so purple into white passes
/// through green and orange into sky blue through yellow - which is what
/// the fixed-point library can afford on a microcontroller and what a rig
/// cannot show. Everything here that mixes two colours does it one of two
/// ways a lamp can: as light (RGB, what two lamps on one surface make) or
/// round the short side of the wheel (HSV, what a gradient between two
/// saturated colours should stay).
///

namespace edmx
{
    struct Rgb
    {
        float r{0.0f};
        float g{0.0f};
        float b{0.0f};
    };

    inline Rgb toRgb(const ecore::HSV& hsv)
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

    inline ecore::HSV toHsv(const Rgb& rgb)
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
        return ecore::HSV(hue, sat, maxC);
    }

    /// A cross-fade between two colours the way two lamps make one: in RGB.
    ///
    /// The brightness is lerped on its own and put back over the mix. Two
    /// saturated colours mixed in RGB meet at a colour with half the level
    /// of either - a pink into a green dips to a dim grey in the middle -
    /// and a lamp cross-fading should not go dark on the way. The hue and
    /// the saturation take the RGB path; the level takes the straight one.
    inline ecore::HSV blendRgb(const ecore::HSV& a, const ecore::HSV& b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const Rgb ra = toRgb(a);
        const Rgb rb = toRgb(b);
        ecore::HSV out = toHsv({ra.r + (rb.r - ra.r) * t,
                                ra.g + (rb.g - ra.g) * t,
                                ra.b + (rb.b - ra.b) * t});
        out.setBrightnessAlpha(a.getValFloat() + (b.getValFloat() - a.getValFloat()) * t);
        return out;
    }

    /// The hue `t` of the way from `a` to `b` round the short side of the
    /// wheel, in degrees.
    inline float blendHue(float a, float b, float t)
    {
        float delta = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
        return std::fmod(a + delta * t + 720.0f, 360.0f);
    }

    /// A cross-fade between two colours that stays a colour: the hue goes
    /// round the short side of the wheel, the saturation and the level
    /// straight. Where blendRgb greys out between two saturated colours -
    /// orange into sky blue is a light grey halfway - this is a dusk. A
    /// gradient of saturated bands wants this; a lamp being added to
    /// another wants blendRgb.
    ///
    /// A colour with no saturation has no hue to speak of, so a fade to or
    /// from one keeps the other side's hue and lets the saturation do it.
    inline ecore::HSV blendHsv(const ecore::HSV& a, const ecore::HSV& b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const float satA = a.getSatFloat();
        const float satB = b.getSatFloat();
        float hue;
        if (satA <= 0.001f)      hue = b.getHueFloat();
        else if (satB <= 0.001f) hue = a.getHueFloat();
        else                     hue = blendHue(a.getHueFloat(), b.getHueFloat(), t);
        const float val = a.getValFloat() + (b.getValFloat() - a.getValFloat()) * t;
        ecore::HSV out(hue, satA + (satB - satA) * t, val);
        out.setBrightnessAlpha(val);
        return out;
    }

    /// Two lamps on one surface: the light sums, channel by channel, and
    /// clamps. A lamp at black adds nothing, and a white one over a purple
    /// wash makes a paler purple - not, as an HSV lerp of the two would
    /// have it, a green.
    inline ecore::HSV addRgb(const ecore::HSV& base, const ecore::HSV& over)
    {
        const Rgb a = toRgb(base);
        const Rgb b = toRgb(over);
        return toHsv({std::min(a.r + b.r, 1.0f),
                      std::min(a.g + b.g, 1.0f),
                      std::min(a.b + b.b, 1.0f)});
    }
}
