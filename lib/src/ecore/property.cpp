// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "property.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>

#include "logging.h"

using namespace ecore;

float Property::get() const
{
    switch (type)
    {
        case Type::Bool:  return (boolValue != nullptr && *boolValue) ? 1.0f : 0.0f;
        case Type::Float: return (floatValue != nullptr) ? *floatValue : 0.0f;
        default:          return 0.0f;
    }
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
    else if (type == Type::Float)
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
    else
    {
        return; // one number is not a colour or a shape
    }

    changed();
}

HSV Property::getColor() const
{
    return (type == Type::Color && colorValue != nullptr) ? *colorValue : HSV();
}

void Property::setColor(const HSV& color)
{
    if (type != Type::Color || colorValue == nullptr)
    {
        return;
    }
    *colorValue = color;
    changed();
}

void Property::changed() const
{
    if (onChanged)
    {
        onChanged();
    }
}


void PropertyBag::push(Property&& property)
{
    property.name = prefix + property.name;

#if ERROR_CHECKING_ENABLED
    if (find(property.name) != nullptr)
    {
        log::dbgLog("PropertyBag: '" + property.name + "' registered twice, second ignored",
                    log::Verbosity::Error, log::Category::Library);
        return;
    }
#endif

    properties.push_back(std::move(property));
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
    push(std::move(property));
}

void PropertyBag::add(const char* name, bool& value, std::function<void()> onChanged)
{
    Property property;
    property.name = name;
    property.type = Property::Type::Bool;
    property.boolValue = &value;
    property.onChanged = std::move(onChanged);
    push(std::move(property));
}

void PropertyBag::add(const char* name, HSV& value, std::function<void()> onChanged)
{
    Property property;
    property.name = name;
    property.type = Property::Type::Color;
    property.colorValue = &value;
    property.onChanged = std::move(onChanged);
    push(std::move(property));
}

void PropertyBag::add(const char* name, eanim::AutomationCurve& value, std::function<void()> onChanged)
{
    Property property;
    property.name = name;
    property.type = Property::Type::Curve;
    property.curveValue = &value;
    property.onChanged = std::move(onChanged);
    push(std::move(property));
}

void PropertyBag::addState(const char* name, float& value, std::function<void()> onChanged)
{
    Property property;
    property.name = name;
    property.type = Property::Type::Float;
    property.role = Property::Role::State;
    property.floatValue = &value;
    property.minValue = Property::STATE_MIN;
    property.maxValue = Property::STATE_MAX;
    property.onChanged = std::move(onChanged);
    push(std::move(property));
}

void PropertyBag::reflect(const char* childName, Reflectable& child)
{
    const std::string outer = prefix;
    prefix += childName;
    prefix.push_back('.');
    child.reflect(*this);
    prefix = outer;
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

bool PropertyBag::setColor(const std::string& name, const HSV& color)
{
    for (Property& property : properties)
    {
        if (property.name == name && property.type == Property::Type::Color)
        {
            property.setColor(color);
            return true;
        }
    }
    return false;
}


std::string ecore::serializeState(const PropertyBag& bag)
{
    std::string out;
    char number[32];
    for (const Property& property : bag.all())
    {
        if (!property.isState())
        {
            continue;
        }
        // %.9g: every float round-trips exactly, so a copy put at this
        // state draws the same pixel, not one a rounding away from it
        std::snprintf(number, sizeof(number), "%.9g", property.get());
        if (!out.empty())
        {
            out.push_back(' ');
        }
        out += property.name;
        out.push_back('=');
        out += number;
    }
    return out;
}

int ecore::applyState(PropertyBag& bag, const std::string& text)
{
    int applied = 0;
    size_t at = 0;
    while (at < text.size())
    {
        // one `name=value` token, however much whitespace is around it
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t'))
        {
            ++at;
        }
        size_t end = at;
        while (end < text.size() && text[end] != ' ' && text[end] != '\t')
        {
            ++end;
        }
        if (end > at)
        {
            const std::string token = text.substr(at, end - at);
            const size_t eq = token.find('=');
            if (eq != std::string::npos && eq > 0)
            {
                char* stop = nullptr;
                const float value = std::strtof(token.c_str() + eq + 1, &stop);
                if (stop != token.c_str() + eq + 1 && bag.set(token.substr(0, eq), value))
                {
                    ++applied;
                }
            }
        }
        at = end;
    }
    return applied;
}
