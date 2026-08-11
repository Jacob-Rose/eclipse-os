// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core.h"

namespace ecore
{
    /* @brief One tunable value on a pattern: a float with a range, or a bool.
    *
    * A Property points *at* the member it names rather than holding a copy, so
    * a pattern keeps reading its own field the way it always did and nothing
    * has to be plumbed through a setter. The pattern is the owner; a Property
    * is a view onto it, and must not outlive the object it was built from.
    *
    * Floats and bools only, on purpose. This is for turning knobs on a look
    * while it runs - an attack time, a gain, a flag - and every one of those is
    * one of the two. Anything richer is a config field, not a knob.
    */
    struct Property
    {
        enum class Type
        {
            Float,
            Bool,
        };

        std::string name;
        Type type{Type::Float};

        float* floatValue{nullptr};
        bool* boolValue{nullptr};

        /// The range a slider spans. Meaningless for a bool.
        float minValue{0.0f};
        float maxValue{1.0f};

        /* @brief Run after a set, for a value something else is derived from.
        *
        * An envelope keeps a built curve, not the two numbers it was built
        * from, so writing the number is only half of changing it. Optional:
        * most properties are read straight out of the field every frame and
        * need nothing.
        */
        std::function<void()> onChanged;

        /// Bools read back as 0 or 1, so a UI can treat every property as a number.
        float get() const;

        /// Clamped to the range for a float, and anything non-zero is true for
        /// a bool. Fires onChanged.
        void set(float value);
    };


    /* @brief The tunable properties of one object, gathered on request.
    *
    * Built fresh each time it is asked for rather than kept, because the
    * pointers inside it are only good for as long as the object that filled it
    * is alive - and on a state machine, the object in question changes when the
    * look does.
    *
    * Registering is meant to cost one line per property:
    *
    *     void MyLook::reflect(ecore::PropertyBag& bag)
    *     {
    *         bag.add("gain", gain, 0.0f, 2.0f);
    *         bag.add("smoothing", smoothing, 0.0f, 1.0f);
    *         bag.add("inverted", bInverted);
    *     }
    */
    class PropertyBag
    {
    public:
        void add(const char* name, float& value, float minValue, float maxValue,
                 std::function<void()> onChanged = {});

        void add(const char* name, bool& value, std::function<void()> onChanged = {});

        void clear() { properties.clear(); }
        bool isEmpty() const { return properties.empty(); }
        size_t size() const { return properties.size(); }

        const std::vector<Property>& all() const { return properties; }

        const Property* find(const std::string& name) const;

        /// Sets by name. False if there is no property called that.
        bool set(const std::string& name, float value);

    private:
        std::vector<Property> properties;
    };
}
