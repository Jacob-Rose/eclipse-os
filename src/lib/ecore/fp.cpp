#include "fp.h"

using namespace efp;

float getFloat(fpInt value)
{
    float x = static_cast<float>(value);
    return x / SCALE_FACTOR;
}
