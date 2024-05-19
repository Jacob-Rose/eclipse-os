#include "jmath.h"

#include "jlogging.h"
#include <random>

float lerp(const float& a, const float& b, const float& t)
{
    return ((1.0f - t) * a) + (t * b);
}

float inv_lerp(const float& a, const float& b, const float& t)
{
    return (t - a) / (b - a);
}

float remap(const float& i_min, const float& i_max, const float& o_min, const float& o_max, const float& v)
{
    return lerp(o_min, o_max, inv_lerp(i_min, i_max, v));
}

float lerp_keyframes(float a, const std::vector<float>& keys)
{
    if(a >= 1.0f)
    {
        return keys[keys.size()-1];
    }

    if(a <= 0.0f)
    {
        return keys[0];
    }

    float step = 1.0f / keys.size();
    int lowerPart = std::trunc(a / step);
    float stepAlpha = a / step;
    stepAlpha -= lowerPart;
    //jlog::print("step alpha: " + std::to_string(stepAlpha));
    return lerp(keys[lowerPart], keys[lowerPart + 1], stepAlpha);
}

float get_index_from_alpha(float a, const std::vector<float>& keys)
{
    if(a > 1.0f)
    {
        return keys[keys.size()-1];
    }
        
    a = std::max(0.0f, a);
    float step = 1.0f / keys.size();
    int index = std::round(a / step);
    return keys[index];
}

float get_random_float()
{
    int randValue = random() % 400;
    return ((float)randValue) / 400;
}

float get_random_float_in_range(float min, float max)
{
    float randVal = get_random_float();
    float diff = max - min;
    return (diff * randVal) + min;
}

bool ess_equal(float A, float B,
                         float maxRelDiff)
{
    // Calculate the difference.
    float diff = fabs(A - B);
    A = fabs(A);
    B = fabs(B);
    // Find the largest
    float largest = (B > A) ? B : A;

    if (diff <= largest * maxRelDiff)
        return true;
    return false;
}

float normalize_angle(float angle)
{
    angle = std::fmod(angle, 360.0f);
    if (angle < 0) {
        angle += 360.0f;
    }
    return angle;
}

float radial_lerp(float a, float b, float t)
{
    float diff = b - a;
    if (diff > 180.0f) {
        b -= 360.0f;
    } else if (diff < -180.0f) {
        b += 360.0f;
    }

    float result = a + t * (b - a);
    return normalize_angle(result);
}