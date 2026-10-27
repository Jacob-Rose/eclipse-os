// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "binding.h"

using namespace edmx;

void ChannelBinding::bindChannel(int channel, const char* property)
{
    Entry entry;
    entry.channel = channel;
    entry.property = property;
    bound.push_back(entry);
}

void ChannelBinding::clear()
{
    bound.clear();
}

int ChannelBinding::apply(const ChannelBuffer& channels, ecore::Reflectable& target) const
{
    if (bound.empty())
    {
        return 0;
    }
    ecore::PropertyBag bag;
    target.reflect(bag);
    return apply(channels, bag);
}

int ChannelBinding::apply(const ChannelBuffer& channels, ecore::PropertyBag& bag) const
{
    int landed = 0;
    for (const Entry& entry : bound)
    {
        const ecore::Property* property = bag.find(entry.property);
        if (property == nullptr)
        {
            continue;
        }

        const uint8_t raw = channels.getChannel(entry.channel);
        float value = 0.0f;
        if (property->type == ecore::Property::Type::Float)
        {
            const float unit = static_cast<float>(raw) / 255.0f;
            value = property->minValue + unit * (property->maxValue - property->minValue);
        }
        else if (property->type == ecore::Property::Type::Bool)
        {
            value = raw >= 128 ? 1.0f : 0.0f;
        }
        else
        {
            continue; // a colour or a curve is not one channel
        }

        if (bag.set(entry.property, value))
        {
            ++landed;
        }
    }
    return landed;
}
