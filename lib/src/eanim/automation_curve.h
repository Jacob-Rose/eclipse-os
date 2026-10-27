// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ecore/core.h>
#include <ecore/tickable.h>

#include <external/easing.h>

#include "attribute_float.h"

using namespace ecore;

namespace eanim
{
    /* @brief One keyframe of an AutomationCurve.
    *
    * The easing shapes the segment that leaves this key; the last key's
    * easing is unused. Linear when bUseEasing is false.
    */
    struct AutomationKey
    {
        float time{0.0f};
        float value{0.0f};

        bool bUseEasing{false};
        easing_functions easingFunction{easing_functions::EaseInOutSine};
    };


    /* @brief A value over time, drawn as up to kMaxKeys keyframes.
    *
    * Fixed capacity, no allocation. @see AutomationCurveTrigger to play one
    * from an impulse.
    */
    class AutomationCurve
    {
    public:
        static constexpr int kMaxKeys = 8;

        /// Inserts in time order. False when the curve is full.
        bool addKey(float inTime, float inValue);
        bool addKey(float inTime, float inValue, easing_functions inEasing);

        void clear() { count = 0; }

        int getKeyCount() const { return count; }
        const AutomationKey* getKey(int index) const;

        /// The time of the last key.
        float getDuration() const;

        /// The value at `seconds`; held at the first/last key outside the span.
        float evaluate(float seconds) const;

        /// The largest value in [fromSeconds, toSeconds]: what a frame that
        /// spans a short peak should show. Exact for monotonic easings; the
        /// overshooting ones (Back, Elastic, Bounce) can crest between keys.
        float peak(float fromSeconds, float toSeconds) const;

    private:
        AutomationKey keys[kMaxKeys];
        int count{0};
    };


    /* @brief What a new impulse does to a curve that is still playing.
    */
    enum class RetriggerMode
    {
        /// Restart from the first key, stepping the output down if needed.
        Restart,

        /// Restart the clock but hold the current level until the new pass
        /// climbs above it. Swells instead of dipping on every impulse.
        RestartHold,

        /// Each impulse plays its own voice; live voices combine by
        /// VoiceBlend. Past kMaxVoices the oldest is stolen.
        Overlap,
    };

    /* @brief How overlapping voices combine in RetriggerMode::Overlap.
    */
    enum class VoiceBlend
    {
        Max,    ///< loudest voice wins; stays inside the curve's range
        Sum,    ///< voices add; can exceed the range, clamp downstream
    };


    /* @brief Plays an AutomationCurve from an impulse.
    *
    * A FloatAttribute, so anything that takes one (brightness, a hue shift,
    * a blend weight) can be driven by it.
    *
    *     trigger.curve.addKey(0.0f, 0.0f);
    *     trigger.curve.addKey(0.1f, 1.0f);
    *     trigger.curve.addKey(0.6f, 0.0f);
    *     ...
    *     trigger.trigger();          // when the event lands
    *     trigger.tick(deltaTime);    // once a frame
    *     brightness = trigger.getValue();
    *
    * @see RetriggerMode for what a second impulse does to the first
    */
    class AutomationCurveTrigger : public FloatAttribute, public Tickable
    {
    public:
        /// Voices alive at once in Overlap mode.
        static constexpr int kMaxVoices = 4;

        AutomationCurve curve;

        RetriggerMode retriggerMode{RetriggerMode::RestartHold};
        VoiceBlend voiceBlend{VoiceBlend::Max};

        /// An impulse landed now.
        void trigger() { triggerAt(0.0f); }

        /// An impulse landed `secondsAgo` ago. Use this when the event's real
        /// time is known, so a beat inside a frame is not quantised to it.
        void triggerAt(float secondsAgo);

        /// Back to rest, nothing playing.
        void reset();

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // FloatAttribute interface
        virtual float getValue() const override { return value; }

        bool isPlaying() const;

        /// Seconds into the newest pass, or negative when nothing is playing.
        float getTimeSinceTrigger() const;

    private:
        struct Voice
        {
            bool bActive{false};
            float time{0.0f};
            float previousTime{0.0f};
            /// set by a trigger, cleared by the first tick after it: that
            /// tick reads the voice where it is instead of advancing it
            bool bFresh{false};
        };

        float valueAtRest() const;
        Voice* claimVoice();

        Voice voices[kMaxVoices];

        float value{0.0f};

        /// RestartHold's floor until the new pass beats it
        float holdLevel{0.0f};
        bool bHolding{false};
    };
}
