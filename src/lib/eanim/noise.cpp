#include "noise.h"

using namespace eanim;

PerlinNoiseGenerator2D::PerlinNoiseGenerator2D()
{
    init();
}

void PerlinNoiseGenerator2D::init()
{
    currentTime = 0.0f;
    noise.SetSeed(rand());
}

void PerlinNoiseGenerator2D::tick(float deltaTime)
{
    currentTime += deltaTime;
}

float PerlinNoiseGenerator2D::evaluate(float x, float y) const
{
    float freqScalar = 1.0f / noise.GetFrequency();

    return noise.GetNoise<float>(x * imageScaleX, y * imageScaleY, currentTime * timeScale * freqScalar);
}
