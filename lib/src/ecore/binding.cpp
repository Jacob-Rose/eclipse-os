// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "binding.h"

#include <algorithm>
#include <cmath>

#include "property.h"

using namespace ecore;

bool ecore::landUnit(PropertyBag& bag, const std::string& property, float unit)
{
    const Property* found = bag.find(property);
    if (found == nullptr)
    {
        return false;
    }
    float value = 0.0f;
    if (found->type == Property::Type::Float)
    {
        value = found->minValue + unit * (found->maxValue - found->minValue);
    }
    else if (found->type == Property::Type::Bool)
    {
        value = unit >= 0.5f ? 1.0f : 0.0f;
    }
    else
    {
        return false;   // a colour or a curve is not one number
    }
    return bag.set(property, value);
}

const char* BindingRule::modeName(Mode inMode)
{
    return inMode == Mode::Trigger ? "trigger" : "value";
}

bool BindingRule::parseMode(const std::string& text, Mode& outMode)
{
    if (text == "value") { outMode = Mode::Value; return true; }
    if (text == "trigger") { outMode = Mode::Trigger; return true; }
    return false;
}

float BindingRule::scale(float raw) const
{
    const float span = high - low;
    if (span == 0.0f)
    {
        return 0.0f;
    }
    return std::clamp((raw - low) / span, 0.0f, 1.0f);
}

void BindingRule::reset()
{
    lastSent = -1.0f;
    lastAt = -1.0e9;
    wasUp = false;
}

bool BindingRule::takes(float raw, double now, float& outValue)
{
    const float value = scale(raw);

    if (mode == Mode::Trigger)
    {
        const bool up = value >= threshold;
        const bool fired = up && !wasUp;
        wasUp = up;
        if (!fired)
        {
            return false;
        }
        lastSent = value;
        lastAt = now;
        outValue = 1.0f;
        return true;
    }

    if (now - lastAt < minInterval)
    {
        return false;
    }
    const bool overdue = maxInterval > 0.0 && (now - lastAt) >= maxInterval;
    if (lastSent >= 0.0f && std::fabs(value - lastSent) < minChange && !overdue)
    {
        return false;
    }
    lastSent = value;
    lastAt = now;
    outValue = value;
    return true;
}
