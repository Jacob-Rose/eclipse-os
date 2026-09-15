#include "noise.h"

#include <cmath>

#include "../ecore/math.h"

using namespace eanim;

PerlinNoiseGenerator2D::PerlinNoiseGenerator2D()
{
    init();
}

void PerlinNoiseGenerator2D::init()
{
    currentTime = 0.0f;
    // 16 bits of seed, not rand()'s full width: the seed crosses to a desk
    // as a float in a PropertyBag (see reflectState), and a float carries 24
    // bits exactly - a 31-bit rand() (Linux, the Pico's newlib) does not
    // survive the trip, and a copy seeded from the rounded value draws a
    // different field. 65536 fields is plenty.
    setSeed(rand() & 0xFFFF);
}

void PerlinNoiseGenerator2D::setSeed(int inSeed)
{
    seed = inSeed;
    seedState = static_cast<float>(inSeed);
    noise.SetSeed(inSeed);
}

void PerlinNoiseGenerator2D::reflectState(ecore::PropertyBag& bag, const std::string& prefix)
{
    seedState = static_cast<float>(seed);
    bag.addState((prefix + "seed").c_str(), seedState, [this]() { setSeed(static_cast<int>(seedState)); });
    bag.addState((prefix + "time").c_str(), currentTime);
}

void PerlinNoiseGenerator2D::tick(float deltaTime)
{
    currentTime += deltaTime * timeScale;

    // Loop rather than grow - see kLoopLength. fmod rather than a subtraction
    // so a clock a desk lands from far outside the loop comes back in one tick
    // too, and the second line covers a field running backwards.
    currentTime = std::fmod(currentTime, kLoopLength);
    if (currentTime < 0.0f)
    {
        currentTime += kLoopLength;
    }
}

float PerlinNoiseGenerator2D::evaluate(float x, float y) const
{
    const float freqScalar = 1.0f / noise.GetFrequency();
    const float sx = x * imageScaleX;
    const float sy = y * imageScaleY;

    float value = noise.GetNoise<float>(sx, sy, currentTime * freqScalar);

    // The seam. Sampling at (currentTime - kLoopLength) is sampling just
    // before zero, so by the time the clock wraps this has faded all the way
    // to where the next loop starts, and the wrap itself moves nothing.
    const float fadeStart = kLoopLength - kLoopFade;
    if (currentTime > fadeStart)
    {
        const float alpha = (currentTime - fadeStart) / kLoopFade;
        const float ahead = noise.GetNoise<float>(sx, sy, (currentTime - kLoopLength) * freqScalar);
        value = lerp(value, ahead, alpha);
    }

    return value;
}
