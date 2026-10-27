// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>

namespace ecore
{
    class PropertyBag;

    /// Lands a 0..1 value on a property by name: scaled across a float's own
    /// range, thresholded at a half for a bool, refused for a colour or a
    /// curve. The step every binding ends with, whatever the wire.
    bool landUnit(PropertyBag& bag, const std::string& property, float unit);

    /* @brief When a stream of numbers acts, and with what. The half of a
    * binding that is the same for OSC, MIDI or anything else that sends
    * values: the number is scaled from [low, high] onto 0..1, then either
    * every change acts (rate-limited by an interval and a minimum change,
    * with a keepalive so a steady value is still restated) or the rising
    * edge through a threshold acts once, which is what a beat spike is.
    *
    *     BindingRule rule;
    *     rule.mode = BindingRule::Mode::Trigger;
    *     float value;
    *     if (rule.takes(raw, now, value)) ...
    */
    class BindingRule
    {
    public:
        enum class Mode { Value, Trigger };

        Mode mode{Mode::Value};

        float low{0.0f};
        float high{1.0f};
        float threshold{0.5f};

        double minInterval{1.0 / 30.0};
        double minChange{0.01};
        double maxInterval{0.2};

        /// The incoming number as 0..1 across [low, high], clamped.
        float scale(float raw) const;

        /// Whether to act on this number, and with what: the scaled value,
        /// or 1 for a trigger. The rate limit, the change threshold and the
        /// rising edge all live here.
        bool takes(float raw, double now, float& outValue);

        /// Forgets the last value, so the next number always acts.
        void reset();

        static const char* modeName(Mode mode);
        static bool parseMode(const std::string& text, Mode& outMode);

    private:
        float lastSent{-1.0f};
        double lastAt{-1.0e9};   ///< long ago, so the first number is never inside the interval
        bool wasUp{false};
    };
}
