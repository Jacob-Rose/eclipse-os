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
    /* @brief One thing on an object a desk can reach by name: a float with a
    * range, a bool, a colour, or a curve.
    *
    * A Property points *at* the member it names rather than holding a copy, so
    * the object keeps reading its own field the way it always did and nothing
    * has to be plumbed through a setter. The object is the owner; a Property
    * is a view onto it, and must not outlive the object it was built from.
    *
    * Four types, and no more. A float or a bool covers almost every knob. A
    * colour is here because the alternative was three sliders spelling one
    * out, and nobody picks a colour that way. A curve is here because an
    * envelope is a shape, not a number, and a desk's curve editor wants the
    * live shape to draw and to write back. Anything richer is a config
    * field, not a property.
    *
    * The role says what the property is *for*. A knob is tuning, something
    * you turn while the show runs. State is a clock or a phase - a noise
    * field's time, an LFO's offset - with no range worth a slider; it exists
    * so a copy of the object elsewhere can be put at the same picture. See
    * serializeState().
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

        /// The bounds a state float carries. Wide on purpose: a clock is not a slider.
        static constexpr float STATE_MIN = -1.0e9f;
        static constexpr float STATE_MAX = 1.0e9f;

        std::string name;
        Type type{Type::Float};
        Role role{Role::Knob};

        float* floatValue{nullptr};
        bool* boolValue{nullptr};
        HSV* colorValue{nullptr};
        eanim::AutomationCurve* curveValue{nullptr};

        /// The range a slider spans. Meaningless for a bool, a colour or a curve.
        float minValue{0.0f};
        float maxValue{1.0f};

        /* @brief Run after a set, for a value something else is derived from.
        *
        * An envelope keeps a built table, not the numbers it was built from,
        * so writing the number is only half of changing it. Optional: most
        * properties are read straight out of the field every frame and need
        * nothing. A curve is edited in place through curveValue; call
        * changed() when that is done.
        */
        std::function<void()> onChanged;

        bool isKnob() const { return role == Role::Knob; }
        bool isState() const { return role == Role::State; }

        /// Bools read back as 0 or 1, so a UI can treat every property as a
        /// number. A colour or a curve reads back as zero.
        float get() const;

        /// Clamped to the range for a float, and anything non-zero is true for
        /// a bool. Fires onChanged. Does nothing to a colour or a curve, which
        /// cannot be said as one number.
        void set(float value);

        /// The colour, or black for a property that is not one.
        HSV getColor() const;

        /// Sets a colour property and fires onChanged. Does nothing to the
        /// others: a property is one type, and half-writing it is worse than
        /// refusing.
        void setColor(const HSV& color);

        /// Fires onChanged. For an edit made in place, through curveValue.
        void changed() const;
    };


    /* @brief The properties of one object, gathered on request.
    *
    * Built fresh each time it is asked for rather than kept, because the
    * pointers inside it are only good for as long as the object that filled it
    * is alive - and on a state machine, the object in question changes when the
    * look does.
    *
    * Filled by a Reflectable's reflect(), one line per property. A child
    * that reflects on its own (a noise field, an LFO) goes in under a prefix
    * with reflect(), so its `time` is this object's `noise.time`.
    *
    * Names are unique within a bag by contract. Under ERROR_CHECKING_ENABLED
    * a second add() of a name is refused and logged rather than registered
    * twice: a desk setting "noise" has to know which one it reached.
    */
    class PropertyBag
    {
    public:
        void add(const char* name, float& value, float minValue, float maxValue,
                 std::function<void()> onChanged = {});

        void add(const char* name, bool& value, std::function<void()> onChanged = {});

        void add(const char* name, HSV& value, std::function<void()> onChanged = {});

        void add(const char* name, eanim::AutomationCurve& value, std::function<void()> onChanged = {});

        /* @brief A clock or a phase rather than a knob: Role::State, no range
        * worth a slider. The bounds are wide enough that nothing a clock
        * reaches is clamped.
        */
        void addState(const char* name, float& value, std::function<void()> onChanged = {});

        /* @brief A child's properties, each named `<childName>.<its name>`.
        *
        * The child reflects into this same bag with the prefix applied, so a
        * look that owns a noise field says `bag.reflect("noise", noise)` and
        * the field's `time` and `seed` land as `noise.time`, `noise.seed`.
        * Nests: a child's own children prefix again.
        */
        void reflect(const char* childName, Reflectable& child);

        void clear() { properties.clear(); }
        bool isEmpty() const { return properties.empty(); }
        size_t size() const { return properties.size(); }

        const std::vector<Property>& all() const { return properties; }

        const Property* find(const std::string& name) const;

        /// Sets by name. False if there is no property called that.
        bool set(const std::string& name, float value);

        /// The same for a colour. False if there is no property called that,
        /// *or* if the one there is is not a colour - a swatch dropped onto an
        /// attack time is a mistake, not a conversion.
        bool setColor(const std::string& name, const HSV& color);

    private:
        void push(Property&& property);

        std::vector<Property> properties;
        std::string prefix;
    };


    /* @brief The bag's state - every Role::State float - as text, `name=value`
    * pairs separated by spaces, in registration order; and the way back.
    *
    * This is how a relic's running state crosses a cable: the desk asks, the
    * relic answers with serializeState() of its bag, the desk spawns the same
    * look and applyState()s the answer into it, and from then on the two run
    * in step. Knobs, colours and curves are not state and are skipped.
    *
    * applyState() ignores names the bag does not have and text it cannot read,
    * and returns how many values landed - a relic on newer firmware than the
    * desk may name a clock the desk's copy of the look does not have yet, and
    * that is a value to skip, not a reason to refuse the rest.
    */
    std::string serializeState(const PropertyBag& bag);
    int applyState(PropertyBag& bag, const std::string& text);
}
