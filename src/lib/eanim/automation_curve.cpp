// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "automation_curve.h"

#include <algorithm>

using namespace eanim;

// ============================================================================
// AutomationCurve
// ============================================================================

bool AutomationCurve::addKey(float inTime, float inValue)
{
    AutomationKey key;
    key.time = inTime;
    key.value = inValue;
    key.bUseEasing = false;

    if (count >= kMaxKeys)
    {
        return false;
    }

    // Insertion sort by time. Keys usually arrive in order, so this is a walk
    // to the end and a write; accepting them out of order costs nothing and
    // means a curve can be described in whatever order reads best.
    int index = count;
    while (index > 0 && keys[index - 1].time > inTime)
    {
        keys[index] = keys[index - 1];
        index--;
    }

    keys[index] = key;
    count++;
    return true;
}

bool AutomationCurve::addKey(float inTime, float inValue, easing_functions inEasing)
{
    if (!addKey(inTime, inValue))
    {
        return false;
    }

    // Find what we just inserted rather than assuming where it went, since the
    // insert above sorts. Times are compared exactly because they came from the
    // same argument, not from arithmetic.
    for (int i = 0; i < count; i++)
    {
        if (keys[i].time == inTime && keys[i].value == inValue)
        {
            keys[i].bUseEasing = true;
            keys[i].easingFunction = inEasing;
            break;
        }
    }

    return true;
}

const AutomationKey* AutomationCurve::getKey(int index) const
{
    if (index < 0 || index >= count)
    {
        return nullptr;
    }
    return &keys[index];
}

float AutomationCurve::getDuration() const
{
    return (count > 0) ? keys[count - 1].time : 0.0f;
}

float AutomationCurve::evaluate(float seconds) const
{
    if (count == 0)
    {
        return 0.0f;
    }
    if (count == 1 || seconds <= keys[0].time)
    {
        return keys[0].value;
    }
    if (seconds >= keys[count - 1].time)
    {
        return keys[count - 1].value;
    }

    int segment = 0;
    while (segment < count - 2 && seconds >= keys[segment + 1].time)
    {
        segment++;
    }

    const AutomationKey& from = keys[segment];
    const AutomationKey& to = keys[segment + 1];

    const float span = to.time - from.time;
    if (span <= 0.0f)
    {
        // Two keys at the same time is a step, and the later one wins.
        return to.value;
    }

    float alpha = (seconds - from.time) / span;
    if (from.bUseEasing)
    {
        alpha = static_cast<float>(getEasingFunction(from.easingFunction)(static_cast<double>(alpha)));
    }

    return from.value + ((to.value - from.value) * alpha);
}

float AutomationCurve::peak(float fromSeconds, float toSeconds) const
{
    if (count == 0)
    {
        return 0.0f;
    }
    if (toSeconds < fromSeconds)
    {
        std::swap(fromSeconds, toSeconds);
    }

    float highest = std::max(evaluate(fromSeconds), evaluate(toSeconds));

    // Every key strictly inside the span. A curve's turning points are its
    // keys, so for monotonic easings this is the exact maximum rather than an
    // approximation of it.
    for (int i = 0; i < count; i++)
    {
        if (keys[i].time > fromSeconds && keys[i].time < toSeconds)
        {
            highest = std::max(highest, keys[i].value);
        }
    }

    return highest;
}


// ============================================================================
// AutomationCurveTrigger
// ============================================================================

float AutomationCurveTrigger::valueAtRest() const
{
    // Where the curve leaves things when nothing is playing: its last key. For
    // an envelope that ends at zero this is dark, which is the common case, but
    // a curve that ends lit stays lit rather than snapping back to its start.
    const int count = curve.getKeyCount();
    if (count == 0)
    {
        return 0.0f;
    }
    return curve.getKey(count - 1)->value;
}

AutomationCurveTrigger::Voice* AutomationCurveTrigger::claimVoice()
{
    for (int i = 0; i < kMaxVoices; i++)
    {
        if (!voices[i].bActive)
        {
            return &voices[i];
        }
    }

    // All busy: steal the oldest, which is the one nearest finished and so has
    // the least left to say.
    Voice* oldest = &voices[0];
    for (int i = 1; i < kMaxVoices; i++)
    {
        if (voices[i].time > oldest->time)
        {
            oldest = &voices[i];
        }
    }
    return oldest;
}

void AutomationCurveTrigger::triggerAt(float secondsAgo)
{
    const float startAt = std::max(secondsAgo, 0.0f);

    if (retriggerMode == RetriggerMode::Overlap)
    {
        Voice* voice = claimVoice();
        voice->bActive = true;
        voice->bFresh = true;
        voice->time = startAt;
        voice->previousTime = 0.0f;
        return;
    }

    if (retriggerMode == RetriggerMode::RestartHold)
    {
        // Hold whatever we had reached until the new pass climbs past it. Only
        // worth doing if the new pass actually starts below where we are;
        // otherwise there is nothing to hold and holding would flatten the top
        // of the curve.
        const float startValue = curve.evaluate(startAt);
        if (value > startValue)
        {
            holdLevel = value;
            bHolding = true;
        }
        else
        {
            bHolding = false;
        }
    }
    else
    {
        bHolding = false;
    }

    // Restart and RestartHold are one voice. The others are dropped, so a mode
    // change while several are live does not leave the extras running.
    for (int i = 1; i < kMaxVoices; i++)
    {
        voices[i].bActive = false;
    }

    voices[0].bActive = true;
    voices[0].bFresh = true;
    voices[0].time = startAt;
    voices[0].previousTime = 0.0f;
}

void AutomationCurveTrigger::reset()
{
    for (int i = 0; i < kMaxVoices; i++)
    {
        voices[i].bActive = false;
        voices[i].bFresh = false;
        voices[i].time = 0.0f;
        voices[i].previousTime = 0.0f;
    }

    bHolding = false;
    holdLevel = 0.0f;
    value = valueAtRest();
}

void AutomationCurveTrigger::tick(float deltaTime)
{
    const float duration = curve.getDuration();

    bool bAnyLive = false;
    float blended = 0.0f;

    for (int i = 0; i < kMaxVoices; i++)
    {
        Voice& voice = voices[i];
        if (!voice.bActive)
        {
            continue;
        }

        if (voice.bFresh)
        {
            // Already sitting at the impulse; advancing here as well would run
            // the pass a frame ahead of the event that started it.
            voice.bFresh = false;
        }
        else
        {
            voice.previousTime = voice.time;
            voice.time += deltaTime;
        }

        // The peak across the frame, not the value at the end of it - see
        // AutomationCurve::peak. On the frame an impulse lands that span runs
        // from the impulse itself, so a pass that starts and crests inside one
        // frame is still read at full height.
        const float contribution = curve.peak(voice.previousTime, voice.time);

        if (voiceBlend == VoiceBlend::Sum)
        {
            blended += contribution;
        }
        else
        {
            blended = bAnyLive ? std::max(blended, contribution) : contribution;
        }
        bAnyLive = true;

        // Retire it once it is past the end. Done after the read, so the frame
        // a voice finishes on still shows its last value rather than skipping
        // to rest a frame early.
        if (voice.time >= duration)
        {
            voice.bActive = false;
        }
    }

    value = bAnyLive ? blended : valueAtRest();

    if (bHolding)
    {
        if (value >= holdLevel)
        {
            // The new pass has caught up; the curve owns the output again.
            bHolding = false;
        }
        else
        {
            value = holdLevel;
        }
    }
}

bool AutomationCurveTrigger::isPlaying() const
{
    for (int i = 0; i < kMaxVoices; i++)
    {
        if (voices[i].bActive)
        {
            return true;
        }
    }
    return false;
}

float AutomationCurveTrigger::getTimeSinceTrigger() const
{
    float newest = -1.0f;
    for (int i = 0; i < kMaxVoices; i++)
    {
        if (voices[i].bActive && (newest < 0.0f || voices[i].time < newest))
        {
            newest = voices[i].time;
        }
    }
    return newest;
}
