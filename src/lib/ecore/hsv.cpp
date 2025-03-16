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
    alphaAsFloat = std::clamp(alphaAsFloat, 0.0f, 1.0f);
    fpInt alphaAsInt = alphaAsFloat * SCALE_FACTOR;

    fpInt invAlphaAsInt = SCALE_FACTOR - alphaAsInt;

    HSV blendedColor;

    blendedColor.h = (static_cast<uint32_t>(a.h) * invAlphaAsInt) / SCALE_FACTOR + 
                     (static_cast<uint32_t>(b.h) * alphaAsInt) / SCALE_FACTOR;// radialLerp(a.h, b.h, alphaAsInt);

    blendedColor.s = (static_cast<uint32_t>(a.s) * invAlphaAsInt) / SCALE_FACTOR + 
                     (static_cast<uint32_t>(b.s) * alphaAsInt) / SCALE_FACTOR;
    blendedColor.v = (static_cast<uint32_t>(a.v) * invAlphaAsInt) / SCALE_FACTOR + 
                     (static_cast<uint32_t>(b.v) * alphaAsInt) / SCALE_FACTOR;

    return blendedColor;
}

// Perform the radial lerp in fixed-point (using integers)
fpInt HSV::radialLerp(fpInt a, fpInt b, float tAsFloat) {
    fpInt tAsInt = tAsFloat * SCALE_FACTOR;
    return radialLerp(a, b, tAsInt);
}

// Perform the radial lerp in fixed-point (using integers)
fpInt HSV::radialLerp(fpInt a, fpInt b, fpInt tAsInt) {

    // Calculate the shortest path around the circle (using integer math)
    uint32_t diff = b - a;
    if (diff > SCALE_FACTOR / 2) {
        diff -= SCALE_FACTOR; // Go the shorter way
    } else if (diff < ((int32_t)SCALE_FACTOR) / -2) {
        diff += SCALE_FACTOR; // Go the shorter way
    }

    // Perform the interpolation (using fixed-point multiplication)
    fpInt result = a + (uint32_t(tAsInt)) * diff / SCALE_FACTOR;

    result %= SCALE_FACTOR;

    return result;
}

void HSV::setSaturationAlpha(float alpha)
{
    float tmpSat = std::clamp(alpha, 0.f, 1.f);
    s = static_cast<fpInt>(tmpSat * SCALE_FACTOR);
}

void HSV::setBrightnessAlpha(float alpha)
{
    float tmpBrightness = std::clamp(alpha, 0.f, 1.f);
    v = static_cast<fpInt>(tmpBrightness * SCALE_FACTOR);
}

void HSV::setHueDegree(float inDegrees)
{
    float tmpHue = std::fmod(inDegrees, 360.0f);
    tmpHue /= 360;
    h = static_cast<fpInt>(tmpHue * SCALE_FACTOR);
}


float HSV::getHueFloat() const
{
    float val = static_cast<float>(h);
    return (val / SCALE_FACTOR) * 360.f;
}

float HSV::getSatFloat() const
{
    float val = static_cast<float>(s);
    return val / SCALE_FACTOR;
}

float HSV::getValFloat() const
{
    float val = static_cast<float>(v);
    return val / SCALE_FACTOR;
}

uint8_t HSV::getValAs8() const
{
    uint32_t v32 = (((uint32_t)v) * 255) / SCALE_FACTOR;
    return v32;
}

uint8_t HSV::getSatAs8() const
{
    uint32_t s32 = (((uint32_t)s) * 255) / SCALE_FACTOR;
    return s32;
}

uint16_t HSV::getHueAs16() const
{
    uint32_t h32 = (static_cast<uint32_t>(h) * UINT16_MAX) / SCALE_FACTOR;
    return static_cast<uint16_t>(h32);
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