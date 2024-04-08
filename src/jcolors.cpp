#include "jcolors.h"

using namespace j;

HSV::HSV(float inH, float inS, float inV)
{
    h = std::fmod(inH, 1.0f);
    h = std::lround(inH * sizeof(uint16_t));
    s = std::lround(inS * sizeof(uint8_t));
    v = std::lround(inV * sizeof(uint8_t));
}

HSV::HSV(uint16_t inH, uint8_t inS, uint8_t inV) : h(inH), s(inS), v(inV)
{

}

HSV::HSV() : h(0), s(0), v(0)
{

}

HSVPalette::HSVPalette()
{
    
}

HSVPalette::HSVPalette(const std::vector<HSV>& inColors)
{
    colors = inColors;
}

HSV HSVPalette::getColor(float val) const
{
    float targetIdx = val * colors.size();
    uint16_t idx = std::trunc(targetIdx);
    uint16_t nextIdx = (idx + 1) % colors.size(); // can loop back on itself
    float alpha = targetIdx - idx;
    
    HSV blendedColor;
    blendedColor.h = colors[idx].h * alpha + 
                     colors[nextIdx].h * alpha; 
    blendedColor.s = colors[idx].s * alpha + 
                     colors[nextIdx].s * alpha;
    blendedColor.v = colors[idx].v * alpha + 
                     colors[nextIdx].v * alpha;

    return blendedColor;
}