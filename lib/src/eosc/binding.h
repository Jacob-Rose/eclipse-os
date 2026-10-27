// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ecore/binding.h>
#include <ecore/name.h>

#include "message.h"

namespace ecore
{
    class PropertyBag;
    class Reflectable;
}

namespace eosc
{
    /* @brief One incoming address bound to one thing. A glob says which
    * addresses, `argument` which number in them, and the ecore::BindingRule
    * it is built on says when to act. `exclude` vetoes matches, because the
    * useful globs overlap: the whole spectrum is `*level*` minus the bands.
    *
    * It lands on a property by name, or fires a trigger tag, or just reports:
    * a desk hands fired bindings to its own actions.
    *
    *     Binding bass;
    *     bass.label = "bass";
    *     bass.pattern = "*basslevel*";
    *     bass.property = "gain";
    *
    * @see BindingSet to run a map of them
    */
    class Binding : public ecore::BindingRule
    {
    public:
        std::string label{"binding"};
        std::string pattern{"*"};
        std::vector<std::string> exclude;
        int argument{0};
        bool enabled{true};

        /// A reflected property to land the value on, when there is one.
        std::string property;
        /// A trigger to fire, when there is one.
        ecore::GameplayTag tag;

        bool matches(const std::string& address) const;

        /// Whether to act on this message, and with what value.
        bool fires(const Message& message, double now, float& outValue);
    };

    /* @brief The bindings of a map, run together against each message.
    *
    *     BindingSet map;
    *     map.add(bass);
    *     map.handle(message, now, [](const Binding& binding, float value) { ... });
    *     map.apply(message, now, look);   // the ones with a property or a tag, straight onto it
    */
    class BindingSet
    {
    public:
        using Fired = std::function<void(const Binding& binding, float value)>;

        void add(const Binding& binding) { bindings.push_back(binding); }
        void clear() { bindings.clear(); }
        size_t size() const { return bindings.size(); }
        Binding* find(const std::string& label);
        const std::vector<Binding>& all() const { return bindings; }
        std::vector<Binding>& all() { return bindings; }

        /// Fired for a binding with a tag, when apply() runs it.
        std::function<bool(const ecore::GameplayTag&)> trigger;

        /// Runs every enabled binding; `fired` is called for each that acts.
        /// Returns how many did.
        int handle(const Message& message, double now, const Fired& fired);

        /// Lands the bindings that name a property on a look and fires the
        /// ones with a tag. Returns how many acted.
        int apply(const Message& message, double now, ecore::PropertyBag& bag);
        int apply(const Message& message, double now, ecore::Reflectable& target);

    private:
        std::vector<Binding> bindings;
    };
}
