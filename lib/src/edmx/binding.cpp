// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "binding.h"

#include <ecore/binding.h>

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
        const float unit = static_cast<float>(channels.getChannel(entry.channel)) / 255.0f;
        if (ecore::landUnit(bag, entry.property, unit))
        {
            ++landed;
        }
    }
    return landed;
}
