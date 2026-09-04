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

namespace ecore
{
    /* @brief One tunable value on a pattern: a float with a range, a bool, or a
    * colour.
    *
    * A Property points *at* the member it names rather than holding a copy, so
    * a pattern keeps reading its own field the way it always did and nothing
    * has to be plumbed through a setter. The pattern is the owner; a Property
    * is a view onto it, and must not outlive the object it was built from.
    *
    * Three types, and no more. A knob is something you turn while the show is
    * running - an attack time, a gain, a flag - and a float or a bool covers
    * almost all of it. A colour is here because the alternative was three
    * sliders spelling one out in hue, saturation and value, and nobody picks a
    * colour that way; it is one value to the desk and one swatch in a UI.
    * Anything richer than these is a config field, not a knob.
    */
    struct Property
    {
        enum class Type
        {
            Float,
            Bool,
            Color,
        };

        /// The bounds addState() uses. Wide on purpose: a clock is not a slider.
        static constexpr float STATE_MIN = -1.0e9f;
        static constexpr float STATE_MAX = 1.0e9f;

        std::string name;
        Type type{Type::Float};

        float* floatValue{nullptr};
        bool* boolValue{nullptr};
        HSV* colorValue{nullptr};

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
        /// A colour reads back as zero; ask getColor() for it.
        float get() const;

        /// Clamped to the range for a float, and anything non-zero is true for
        /// a bool. Fires onChanged. Does nothing to a colour, which cannot be
        /// said as one number.
        void set(float value);

        /// The colour, or black for a property that is not one.
        HSV getColor() const;

        /// Sets a colour property and fires onChanged. Does nothing to the
        /// other two: a knob is one type, and half-writing it is worse than
        /// refusing.
        void setColor(const HSV& color);
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
    *         bag.add("color", tint);
    *     }
    */
    class PropertyBag
    {
    public:
        void add(const char* name, float& value, float minValue, float maxValue,
                 std::function<void()> onChanged = {});

        void add(const char* name, bool& value, std::function<void()> onChanged = {});

        void add(const char* name, HSV& value, std::function<void()> onChanged = {});

        void clear() { properties.clear(); }
        bool isEmpty() const { return properties.empty(); }
        size_t size() const { return properties.size(); }

        const std::vector<Property>& all() const { return properties; }

        const Property* find(const std::string& name) const;

        /// Sets by name. False if there is no property called that.
        bool set(const std::string& name, float value);

        /* @brief A clock or a phase rather than a knob: no range worth a slider.
        *
        * reflectState() registers with this - the noise field's time, an LFO's
        * offset - so the same bag type carries a look's running state as
        * carries its tuning, and the same set() writes it back. The bounds
        * are wide enough that nothing a clock reaches is clamped.
        */
        void addState(const char* name, float& value, std::function<void()> onChanged = {});

        /// The same for a colour. False if there is no property called that,
        /// *or* if the one there is is not a colour - a swatch dropped onto an
        /// attack time is a mistake, not a conversion.
        bool setColor(const std::string& name, const HSV& color);

    private:
        std::vector<Property> properties;
    };


    /* @brief A bag's values as text, `name=value` pairs separated by spaces,
    * in registration order - and the way back.
    *
    * This is how a relic's running state crosses a cable: the desk asks, the
    * relic answers with serializeState() of its reflectState() bag, the desk
    * spawns the same look and applyState()s the answer into it, and from then
    * on the two run in step. Floats only; a bool is 0/1 and a colour is
    * skipped, because neither is a clock.
    *
    * applyState() ignores names the bag does not have and text it cannot read,
    * and returns how many values landed - a relic on newer firmware than the
    * desk may name a clock the desk's copy of the look does not have yet, and
    * that is a value to skip, not a reason to refuse the rest.
    */
    std::string serializeState(const PropertyBag& bag);
    int applyState(PropertyBag& bag, const std::string& text);



    /* @brief A bag's values as text, `name=value` pairs separated by spaces,
    * in registration order - and the way back.
    *
    * This is how a relic's running state crosses a cable: the desk asks, the
    * relic answers with serializeState() of its reflectState() bag, the desk
    * spawns the same look and applyState()s the answer into it, and from then
    * on the two run in step. Floats only; a bool is 0/1 and a colour is
    * skipped, because neither is a clock.
    *
    * applyState() ignores names the bag does not have and text it cannot read,
    * and returns how many values landed - a relic on newer firmware than the
    * desk may name a clock the desk's copy of the look does not have yet, and
    * that is a value to skip, not a reason to refuse the rest.
    */
    std::string serializeState(const PropertyBag& bag);
    int applyState(PropertyBag& bag, const std::string& text);

}
