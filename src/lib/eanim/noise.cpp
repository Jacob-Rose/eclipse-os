#include "noise.h"

using namespace eanim;

PerlinNoiseGenerator2D::PerlinNoiseGenerator2D()
{
    init();
}

void PerlinNoiseGenerator2D::init()
{
    currentTime = 0.0f;
    setSeed(rand());
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
}

float PerlinNoiseGenerator2D::evaluate(float x, float y) const
{
    float freqScalar = 1.0f / noise.GetFrequency();

    return noise.GetNoise<float>(x * imageScaleX, y * imageScaleY, currentTime * freqScalar);
}
