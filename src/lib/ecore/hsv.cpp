// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "hsv.h"

#include <limits>
#include <algorithm>

#include "math.h"

using namespace ecore;

HSV::HSV(const float& inH, const float& inS, const float& inV)
{
    setHueDegree(inH);
    setSaturationAlpha(inS);
    setBrightnessAlpha(inV);
}

HSV::HSV(const fpInt& inH, const fpInt& inS, const fpInt& inV)
{
    h = inH;
    s = inS;
    v = inV;
}

HSV::HSV() : h(0), s(0), v(0)
{

}

void HSV::blendWith(const HSV& otherColor, float alphaAsFloat)
{
    HSV newColor = blend(*this, otherColor, alphaAsFloat);
    h = newColor.h;
    s = newColor.s;
    v = newColor.v;
}

HSV HSV::blend(const HSV& a, const HSV& b, float alphaAsFloat)
{
    const fpInt t = efp::fromUnit(alphaAsFloat);

    HSV blendedColor;
    blendedColor.h = efp::lerpTurns(a.h, b.h, t); // the short way round the hue circle
    blendedColor.s = efp::lerp(a.s, b.s, t);
    blendedColor.v = efp::lerp(a.v, b.v, t);
    return blendedColor;
}

HSV HSV::add(const HSV& a, const HSV& b)
{
    HSV out;
    out.h = efp::lerpTurns(a.h, b.h, efp::HALF); // halfway round the short arc
    out.s = efp::addClamped(a.s, b.s);
    out.v = efp::addClamped(a.v, b.v);
    return out;
}

void HSV::setSaturationAlpha(float alpha)
{
    s = efp::fromUnit(alpha);
}

void HSV::setBrightnessAlpha(float alpha)
{
    v = efp::fromUnit(alpha);
}

void HSV::setHueDegree(float inDegrees)
{
    h = efp::fromTurns(inDegrees * (1.0f / 360.0f));
}

float HSV::getHueFloat() const
{
    return efp::toFloat(h) * 360.f;
}

float HSV::getSatFloat() const
{
    return efp::toFloat(s);
}

float HSV::getValFloat() const
{
    return efp::toFloat(v);
}

uint8_t HSV::getValAs8() const
{
    return efp::to8(v);
}

uint8_t HSV::getSatAs8() const
{
    return efp::to8(s);
}

uint16_t HSV::getHueAs16() const
{
    return efp::turnsTo16(h);
}


/// =========================================================
/// HSVPalette
///


HSVPalette::HSVPalette()
{

}

HSVPalette::HSVPalette(const std::vector<HSV>& inColors)
{
    // Ensure the input contains more than one color
    if (inColors.empty()) {
        stops.clear();  // No colors to work with, clear the stops
        return;
    }

    stops.resize(inColors.size());

    stops[0].position = 0;
    stops[0].color = inColors[0];

    for(int i = 1; i < inColors.size(); ++i)
    {
        stops[i].color = inColors[i];
        stops[i].position = ((float)i) / (inColors.size()-1);
    }
}

HSVPalette::HSVPalette(const std::initializer_list<HSV>& inColors)
{
    // Ensure the input contains more than one color
    if (inColors.size()==0) {
        stops.clear();  // No colors to work with, clear the stops
        return;
    }

    stops.resize(inColors.size());

    stops[0].position = 0;
    stops[0].color = inColors.begin()[0];

    for(int i = 1; i < inColors.size(); ++i)
    {
        stops[i].color = inColors.begin()[i];
        stops[i].position = ((float)i) / (inColors.size()-1);
    }
}

HSVPalette::HSVPalette(const std::vector<HSVPalette::HSVStop>& inColorStops)
{
    stops = inColorStops;
}


HSVPalette::HSVPalette(const std::initializer_list<HSVPalette::HSVStop>& inColorStops)
{
    stops = inColorStops;
}

HSV HSVPalette::getColor(float t) const
{
    // Ensure stops are sorted by position
    if (stops.empty()) return HSV(); // Default to black
    if (t <= stops.front().position) return stops.front().color;
    if (t >= stops.back().position) return stops.back().color;

    //return stops[1].color; // KILL ME

    // Find the two stops that t is between
    for (size_t i = 0; i < stops.size() - 1; ++i) {
        const auto& stop1 = stops[i];
        const auto& stop2 = stops[i + 1];
        if (t >= stop1.position && t <= stop2.position) {
            // Interpolate between these two stops
            float localT = (t - stop1.position) / (stop2.position - stop1.position);
            return HSV::blend(stop1.color, stop2.color, localT);
        }
    }

    return HSV(); // Should not reach here
}

std::string HSV::to_string() const{
    std::string str = "hue: " + std::to_string(getHueAs16());
    str += " sat: " + std::to_string(getSatAs8());
    str += " val: " + std::to_string(getValAs8());
    return str;
}