// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "binding.h"

#include <ecore/property.h>
#include <ecore/reflectable.h>

#include "address.h"

using namespace eosc;

bool Binding::matches(const std::string& address) const
{
    if (!matchAddress(pattern, address))
    {
        return false;
    }
    for (const std::string& veto : exclude)
    {
        if (matchAddress(veto, address))
        {
            return false;
        }
    }
    return true;
}

bool Binding::fires(const Message& message, double now, float& outValue)
{
    if (!enabled || !matches(message.address))
    {
        return false;
    }
    float raw = 0.0f;
    if (!message.number(argument, raw))
    {
        return false;
    }
    return takes(raw, now, outValue);
}

Binding* BindingSet::find(const std::string& label)
{
    for (Binding& binding : bindings)
    {
        if (binding.label == label)
        {
            return &binding;
        }
    }
    return nullptr;
}

int BindingSet::handle(const Message& message, double now, const Fired& fired)
{
    int count = 0;
    for (Binding& binding : bindings)
    {
        float value = 0.0f;
        if (binding.fires(message, now, value))
        {
            ++count;
            if (fired)
            {
                fired(binding, value);
            }
        }
    }
    return count;
}

int BindingSet::apply(const Message& message, double now, ecore::PropertyBag& bag)
{
    int acted = 0;
    for (Binding& binding : bindings)
    {
        const bool hasTag = binding.tag.isValid();
        if (binding.property.empty() && !hasTag)
        {
            continue;
        }
        float value = 0.0f;
        if (!binding.fires(message, now, value))
        {
            continue;
        }
        if (!binding.property.empty() && ecore::landUnit(bag, binding.property, value))
        {
            ++acted;
        }
        if (hasTag && trigger && trigger(binding.tag))
        {
            ++acted;
        }
    }
    return acted;
}

int BindingSet::apply(const Message& message, double now, ecore::Reflectable& target)
{
    ecore::PropertyBag bag;
    target.reflect(bag);
    return apply(message, now, bag);
}
