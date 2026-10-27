// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core.h"
#include "hsv.h"
#include "reflectable.h"

namespace eanim
{
    class AutomationCurve;
}

namespace ecore
{
    /* @brief One named property of an object: a float with a range, a bool, a
    * colour or a curve.
    *
    * A Property points at the member it names; it does not hold a copy. The
    * object stays the owner, and a Property must not outlive it. The role says
    * what it is for: a Knob is tuning a desk shows a widget for, State is a
    * clock or a phase that serializeState() carries so a copy of the object
    * elsewhere can match it.
    */
    struct Property
    {
        enum class Type
        {
            Float,
            Bool,
            Color,
            Curve,
        };

        enum class Role
        {
            Knob,
            State,
        };

        /// the range a state float carries; wide, since a clock is not a slider
        static constexpr float STATE_MIN = -1.0e9f;
        static constexpr float STATE_MAX = 1.0e9f;

        std::string name;
        Type type{Type::Float};
        Role role{Role::Knob};

        float* floatValue{nullptr};
        bool* boolValue{nullptr};
        HSV* colorValue{nullptr};
        eanim::AutomationCurve* curveValue{nullptr};

        /// the range a slider spans; meaningless for a bool, colour or curve
        float minValue{0.0f};
        float maxValue{1.0f};

        /// Optional. Runs after a set, for a value something else is rebuilt
        /// from. A curve edited in place through curveValue calls changed().
        std::function<void()> onChanged;

        bool isKnob() const { return role == Role::Knob; }
        bool isState() const { return role == Role::State; }

        /// The value as a number: a bool reads 0 or 1, a colour or curve 0.
        float get() const;

        /// Sets a float (clamped to the range) or a bool (non-zero is true)
        /// and fires onChanged. Does nothing to a colour or a curve.
        void set(float value);

        /// The colour, or black for a property that is not one.
        HSV getColor() const;

        /// Sets a colour property and fires onChanged. Does nothing to the others.
        void setColor(const HSV& color);

        /// Fires onChanged, for an edit made in place through curveValue.
        void changed() const;
    };


    /* @brief The properties of one object, gathered by its reflect().
    *
    * Build one fresh each time it is needed: it holds pointers into whatever
    * filled it, and on a state machine that object changes with the cue.
    * Names are unique within a bag; under ERROR_CHECKING_ENABLED a second
    * add() of a name is refused and logged.
    *
    *     ecore::PropertyBag bag;
    *     look.reflect(bag);
    *     bag.set("gain", 1.5f);
    *     const ecore::Property* p = bag.find("noise.time");
    *
    * @see ecore::Reflectable for the registering side
    */
    class PropertyBag
    {
    public:
        /// A float knob with the range its slider spans.
        void add(const char* name, float& value, float minValue, float maxValue,
                 std::function<void()> onChanged = {});

        void add(const char* name, bool& value, std::function<void()> onChanged = {});

        void add(const char* name, HSV& value, std::function<void()> onChanged = {});

        void add(const char* name, eanim::AutomationCurve& value, std::function<void()> onChanged = {});

        /// A clock or a phase: Role::State, no slider range.
        void addState(const char* name, float& value, std::function<void()> onChanged = {});

        /// A child's properties, each named `<childName>.<its name>`. Nests.
        void reflect(const char* childName, Reflectable& child);

        void clear() { properties.clear(); }
        bool isEmpty() const { return properties.empty(); }
        size_t size() const { return properties.size(); }

        const std::vector<Property>& all() const { return properties; }

        const Property* find(const std::string& name) const;

        /// Sets by name. False if there is no property called that.
        bool set(const std::string& name, float value);

        /// Sets a colour by name. False if there is no such property or it is
        /// not a colour.
        bool setColor(const std::string& name, const HSV& color);

    private:
        void push(Property&& property);

        std::vector<Property> properties;
        std::string prefix;
    };


    /// The bag's Role::State floats as `name=value` text, space separated, in
    /// registration order. How a relic's running clocks cross the cable so a
    /// copy of its look on the desk runs in step.
    std::string serializeState(const PropertyBag& bag);

    /// The way back. Unknown names and unreadable text are skipped; returns
    /// how many values landed.
    int applyState(PropertyBag& bag, const std::string& text);
}
