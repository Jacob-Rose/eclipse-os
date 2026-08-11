// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "property.h"

#include <algorithm>

using namespace ecore;

float Property::get() const
{
    if (type == Type::Bool)
    {
        return (boolValue != nullptr && *boolValue) ? 1.0f : 0.0f;
    }
    return (floatValue != nullptr) ? *floatValue : 0.0f;
}

void Property::set(float value)
{
    if (type == Type::Bool)
    {
        if (boolValue == nullptr)
        {
            return;
        }
        *boolValue = (value != 0.0f);
    }
    else
    {
        if (floatValue == nullptr)
        {
            return;
        }

        // Clamped rather than rejected. The range is what a slider spans, and
        // a value arriving from a text box slightly outside it is a request to
        // go to the end of the slider, not a mistake worth an error.
        *floatValue = std::min(std::max(value, minValue), maxValue);
    }

    if (onChanged)
    {
        onChanged();
    }
}


void PropertyBag::add(const char* name, float& value, float minValue, float maxValue,
                      std::function<void()> onChanged)
{
    Property property;
    property.name = name;
    property.type = Property::Type::Float;
    property.floatValue = &value;
    property.minValue = minValue;
    property.maxValue = maxValue;
    property.onChanged = std::move(onChanged);

    properties.push_back(std::move(property));
}

void PropertyBag::add(const char* name, bool& value, std::function<void()> onChanged)
{
    Property property;
    property.name = name;
    property.type = Property::Type::Bool;
    property.boolValue = &value;
    property.minValue = 0.0f;
    property.maxValue = 1.0f;
    property.onChanged = std::move(onChanged);

    properties.push_back(std::move(property));
}

const Property* PropertyBag::find(const std::string& name) const
{
    for (const Property& property : properties)
    {
        if (property.name == name)
        {
            return &property;
        }
    }
    return nullptr;
}

bool PropertyBag::set(const std::string& name, float value)
{
    for (Property& property : properties)
    {
        if (property.name == name)
        {
            property.set(value);
            return true;
        }
    }
    return false;
}
