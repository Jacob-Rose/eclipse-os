// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../ecore/core.h"
#include "../ecore/tickable.h"

#include "../external/easing.h"

#include "attribute_float.h"

using namespace ecore;

namespace eanim
{
    /* @brief One point on an AutomationCurve, and the shape of the segment that leaves it.
    *
    * The easing belongs to the key the segment *starts* from, so a curve reads
    * top to bottom as "hold this value, get to the next one like this". The
    * last key has no segment after it, so its easing is unused.
    *
    * Linear is the default because easing_functions has no entry for it - see
    * external/easing.h, which starts at EaseInSine.
    */
    struct AutomationKey
    {
        float time{0.0f};
        float value{0.0f};

        bool bUseEasing{false};
        easing_functions easingFunction{easing_functions::EaseInOutSine};
    };


    /* @brief A value over time, described by keyframes rather than by parameters.
    *
    * The point of this over an attack/decay pair is that a curve is not limited
    * to one family of shapes: a hit that snaps up, sits, and trails away is
    * three keys, and so is anything else.
    *
    * Fixed capacity and no allocation, because this compiles for the
    * microcontrollers as well as the desktop.
    *
    * @see AutomationCurveTrigger to play one from an impulse
    */
    class AutomationCurve
    {
    public:
        static constexpr int kMaxKeys = 8;

        /* @brief Adds a key, keeping the list sorted by time.
        *
        * Out of order is fine; the insert finds its place. Returns false if the
        * curve is full, so a caller building one from config can say so.
        */
        bool addKey(float inTime, float inValue);
        bool addKey(float inTime, float inValue, easing_functions inEasing);

        void clear() { count = 0; }

        int getKeyCount() const { return count; }
        const AutomationKey* getKey(int index) const;

        /* @brief How long the curve runs: the time of its last key. */
        float getDuration() const;

        /* @brief The value at `seconds`, clamped to the first and last key outside the span. */
        float evaluate(float seconds) const;

        /* @brief The largest value across [fromSeconds, toSeconds].
        *
        * This exists because a frame is a span, not an instant. A curve whose
        * rise is shorter than a frame would otherwise be sampled at whatever
        * point the frame happened to land on, so its peak would come out at a
        * different height every time it fired - which on a rig reads as an
        * uneven flicker on a perfectly steady beat.
        *
        * The endpoints and every key inside the span are checked, which is
        * exact for monotonic easings. The overshooting ones - Back, Elastic,
        * Bounce - can crest between two keys and are not caught; if a curve
        * leans on one of those for its peak, give it a key at the crest.
        */
        float peak(float fromSeconds, float toSeconds) const;

    private:
        AutomationKey keys[kMaxKeys];
        int count{0};
    };


    /* @brief One named view onto a look's live AutomationCurve.
    *
    * Same contract as ecore::Property: it points *at* the curve rather than
    * holding a copy, the look stays the owner, and the ref must not outlive
    * it. onChanged is for a look that derives something from the shape;
    * most read the curve fresh every frame and need nothing.
    */
    struct CurveRef
    {
        std::string name;
        AutomationCurve* curve{nullptr};
        std::function<void()> onChanged;
    };

    /* @brief The drawable shapes of one look, gathered on request.
    *
    * reflect(), for curves: where a PropertyBag hands out the knobs worth
    * turning, this hands out the shapes worth drawing - an envelope, a
    * swell - so a desk's curve editor can load the live shape and write an
    * edited one back. Built fresh each time it is asked for, for the same
    * lifetime reason as PropertyBag.
    *
    *     void MyLook::reflectCurves(eanim::CurveBag& bag)
    *     {
    *         bag.add("envelope", envelope.curve);
    *     }
    */
    class CurveBag
    {
    public:
        void add(const char* name, AutomationCurve& curve, std::function<void()> onChanged = {})
        {
            refs.push_back(CurveRef{name, &curve, std::move(onChanged)});
        }

        void clear() { refs.clear(); }
        bool isEmpty() const { return refs.empty(); }

        const std::vector<CurveRef>& all() const { return refs; }

        CurveRef* find(const std::string& name)
        {
            for (CurveRef& ref : refs)
            {
                if (ref.name == name)
                {
                    return &ref;
                }
            }
            return nullptr;
        }

    private:
        std::vector<CurveRef> refs;
    };


    /* @brief What a new impulse does to a curve that is still playing. */
    enum class RetriggerMode
    {
        /* @brief The timeline restarts, and the output steps to the curve's first value.
        *
        * The honest choice when the curve finishes before the next impulse. If
        * it does not, this is a visible step back down on every impulse.
        */
        Restart,

        /* @brief The timeline restarts, but the output never falls to do it.
        *
        * The level the curve had reached is held flat until the new pass climbs
        * back above it, and from there the curve takes over. So the impulse
        * still lands on time and still peaks, but a curve longer than the gap
        * between impulses swells instead of cutting to the floor first.
        */
        RestartHold,

        /* @brief Each impulse starts its own voice, and the live ones are combined.
        *
        * Closest to what an impulse on a timeline actually is: an old pass is
        * not cancelled by a new one, it finishes underneath it. Costs a fixed
        * pool of voices, and past that the oldest is stolen.
        */
        Overlap,
    };

    /* @brief How overlapping voices are combined. */
    enum class VoiceBlend
    {
        Max,    ///< the loudest voice wins. Stays inside the curve's own range.
        Sum,    ///< voices add. Can exceed the curve's range; clamp downstream.
    };


    /* @brief Plays an AutomationCurve from an impulse, so a layer can be animated on that timeline.
    *
    * A trigger is a FloatAttribute, so anything that already takes one of those
    * takes this: brightness, a hue shift, a position, a blend weight. What the
    * number means is the consumer's business - this only owns the shape and the
    * clock.
    *
    * Usage is two calls: trigger() when the event lands, tick(deltaTime) once a
    * frame, then getValue().
    *
    * @see AutomationCurve
    * @see RetriggerMode for what a second impulse does to the first
    */
    class AutomationCurveTrigger : public FloatAttribute, public Tickable
    {
    public:
        /* @brief How many passes of the curve can be alive at once in Overlap.
        *
        * Four rather than a number that cannot run out, because the pool is
        * fixed-size for the microcontrollers. Past it the oldest voice is
        * stolen, which is what you want anyway - it is the one nearest done.
        */
        static constexpr int kMaxVoices = 4;

        AutomationCurve curve;

        RetriggerMode retriggerMode{RetriggerMode::RestartHold};
        VoiceBlend voiceBlend{VoiceBlend::Max};

        /* @brief An impulse landed, now. */
        void trigger() { triggerAt(0.0f); }

        /* @brief An impulse landed `secondsAgo` ago.
        *
        * For anything that knows when the event really happened rather than
        * when we got round to asking. A beat lands anywhere inside a frame, so
        * starting its curve at the frame boundary instead would quantise every
        * pass to the frame grid and put a swing on it that the source does not
        * have.
        */
        void triggerAt(float secondsAgo);

        /* @brief Back to rest, with nothing playing. */
        void reset();

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // FloatAttribute interface
        virtual float getValue() const override { return value; }

        bool isPlaying() const;

        /* @brief Seconds into the newest pass, or a negative number if nothing is playing. */
        float getTimeSinceTrigger() const;

    private:
        /* @brief One pass of the curve. `previous` is where the last frame left it,
        * which is what makes the peak-across-a-frame read possible.
        */
        struct Voice
        {
            bool bActive{false};
            float time{0.0f};
            float previousTime{0.0f};

            /* @brief Set by a trigger, cleared by the first tick after it.
            *
            * `time` is already at the impulse for that first frame, so it must
            * not also advance by a delta - that would count the frame twice and
            * run every pass a frame ahead of the event that started it.
            */
            bool bFresh{false};
        };

        float valueAtRest() const;
        Voice* claimVoice();

        Voice voices[kMaxVoices];

        float value{0.0f};

        /* @brief RestartHold's floor: the level to hold until the new pass beats it. */
        float holdLevel{0.0f};
        bool bHolding{false};
    };
}
