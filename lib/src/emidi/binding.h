// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ecore/name.h>
#include <ecore/property.h>
#include <ecore/reflectable.h>

#include "message.h"

namespace emidi
{
    /* @brief Maps MIDI onto a look: a controller onto a property, a note
    * onto a trigger tag.
    *
    * A control change lands on the named property scaled across its range
    * (a bool is on from 64 up); a note on fires its tag through the trigger
    * callback. Channel 0 binds any channel.
    *
    *     binding.bindControl(1, 7, "gain");
    *     binding.bindNote(1, 60, GameplayTag("scanner.ping"));
    *     binding.trigger = [&](const GameplayTag& tag) { return look.onTrigger(tag); };
    *     ...
    *     binding.apply(message, look);   // from a MessageSink
    */
    class Binding
    {
    public:
        struct Control
        {
            int channel{0};        ///< 1..16, or 0 for any
            int controller{0};     ///< 0..127
            std::string property;
        };

        struct Note
        {
            int channel{0};
            int note{0};
            ecore::GameplayTag tag;
        };

        void bindControl(int channel, int controller, const char* property);
        void bindNote(int channel, int note, const ecore::GameplayTag& tag);
        void clear();

        const std::vector<Control>& controls() const { return controlBindings; }
        const std::vector<Note>& notes() const { return noteBindings; }

        /// Where a bound note goes. Returns whether anything answered.
        std::function<bool(const ecore::GameplayTag&)> trigger;

        /// Applies one message to `target`. True when a binding took it.
        bool apply(const Message& message, ecore::Reflectable& target);

        /// The same, against a bag already built from the target.
        bool apply(const Message& message, ecore::PropertyBag& bag);

    private:
        std::vector<Control> controlBindings;
        std::vector<Note> noteBindings;
    };
}
