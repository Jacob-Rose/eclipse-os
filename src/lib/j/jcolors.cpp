#include "jcolors.h"

#include <limits>
#include <algorithm>

using namespace j;

/*
HSV::HSV(float inH, float inS, float inV)
{
    h = std::fmod(inH, 1.0f);
    h = std::lround(inH * sizeof(uint16_t));
    s = std::lround(inS * sizeof(uint16_t));
    v = std::lround(in * sizeof(uint16_t));
}
*/

HSV::HSV(float inH, uint8_t inS, uint8_t inV) : h(inH), s(inS), v(inV)
{

}

HSV::HSV() : h(0), s(0), v(0)
{

}

HSV j::HSV::blendWith(HSV otherColor, float alpha)
{
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    float inv_alpha = 1.0f - alpha;

    HSV blendedColor;
    //TODO change hue blending to support optional saturation consideration for how much impact a color has
    blendedColor.h = h * inv_alpha + 
                     otherColor.h * alpha; 
    blendedColor.s = s * inv_alpha + 
                     otherColor.s * alpha;
    blendedColor.v = v * inv_alpha + 
                     otherColor.v * alpha;

    return blendedColor;
}

void HSV::setBrightnessPercent(float alpha)
{
    alpha = std::clamp(alpha, 0.f, 1.f);
    v = alpha * 255;
}

uint16_t HSV::getHueAs16() const
{
    return (signed int) (h * std::numeric_limits<uint16_t>().max());
}

HSVPalette::HSVPalette()
{

}

HSVPalette::HSVPalette(const std::vector<HSV>& inColors)
{
    colors = inColors;
}

HSVPalette::HSVPalette(const std::initializer_list<HSV>& inColors)
{
    colors = inColors;
}

HSV HSVPalette::getColor(float val) const
{
    if(colors.size() == 0)
    {
        return HSV(0,0,0);
    }

    int valIntToRemove = std::trunc(val);
    val -= valIntToRemove;
    if(valIntToRemove % 2 == 1)
    {
        val = 1.0f - val;
    }

    float targetIdxFloat = val * colors.size();
    uint16_t idx = std::trunc(targetIdxFloat);
    uint16_t nextIdx = (idx + 1) % colors.size();
    float alpha = targetIdxFloat - idx;

    HSV idxColor = colors[idx];
    HSV nextColor = colors[nextIdx];

    HSV blendedColor = idxColor.blendWith(nextColor, alpha);

    return blendedColor;
}

std::string HSV::to_string() const{
    std::string str = "hue: " + std::to_string(h);
    str += " sat: " + std::to_string(s);
    str += " val: " + std::to_string(v);
    return str;
}