// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "binding.h"

using namespace emidi;

void Binding::bindControl(int channel, int controller, const char* property)
{
    Control control;
    control.channel = channel;
    control.controller = controller;
    control.property = property;
    controlBindings.push_back(control);
}

void Binding::bindNote(int channel, int note, const ecore::GameplayTag& tag)
{
    Note bound;
    bound.channel = channel;
    bound.note = note;
    bound.tag = tag;
    noteBindings.push_back(bound);
}

void Binding::clear()
{
    controlBindings.clear();
    noteBindings.clear();
}

bool Binding::apply(const Message& message, ecore::Reflectable& target)
{
    if (message.isNoteOn())
    {
        // a note needs no bag: it goes to the trigger
        bool took = false;
        for (const Note& bound : noteBindings)
        {
            if ((bound.channel == 0 || bound.channel == message.channel()) && bound.note == message.number())
            {
                if (trigger)
                {
                    trigger(bound.tag);
                }
                took = true;
            }
        }
        return took;
    }

    if (!message.isControlChange())
    {
        return false;
    }

    ecore::PropertyBag bag;
    target.reflect(bag);
    return apply(message, bag);
}

bool Binding::apply(const Message& message, ecore::PropertyBag& bag)
{
    if (!message.isControlChange())
    {
        return false;
    }

    bool took = false;
    for (const Control& control : controlBindings)
    {
        if ((control.channel != 0 && control.channel != message.channel())
            || control.controller != message.number())
        {
            continue;
        }

        const ecore::Property* property = bag.find(control.property);
        if (property == nullptr)
        {
            continue;
        }

        const float unit = static_cast<float>(message.value()) / 127.0f;
        float value = unit;
        if (property->type == ecore::Property::Type::Float)
        {
            value = property->minValue + unit * (property->maxValue - property->minValue);
        }
        else if (property->type == ecore::Property::Type::Bool)
        {
            value = message.value() >= 64 ? 1.0f : 0.0f;
        }
        else
        {
            continue; // a colour or a curve is not one number
        }

        bag.set(control.property, value);
        took = true;
    }
    return took;
}
