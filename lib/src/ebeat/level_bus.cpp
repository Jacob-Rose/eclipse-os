// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "level_bus.h"

#include <algorithm>
#include <cstdio>

#include <ecore/property.h>

using namespace ebeat;

const char* ebeat::describeVuSource(VuSource source)
{
    switch (source)
    {
        case VuSource::Instant: return "inst";
        case VuSource::Meter:   return "meter";
        case VuSource::Average: break;
    }
    return "avg";
}

namespace
{
    const char* const kAudioChannelNames[kAudioChannelCount] = {
        "level", "sub", "bass", "mid", "midhigh", "high",
        "hits", "bass_hits", "mid_hits", "midhigh_hits", "high_hits",
        "presence", "bass_presence", "mid_presence", "midhigh_presence", "high_presence",
        "beat", "bpm", "bpm_confidence", "intensity",
        "level_instant", "level_average", "level_meter",
    };
}

const char* ebeat::audioChannelName(AudioChannel channel)
{
    const int index = static_cast<int>(channel);
    if (index < 0 || index >= kAudioChannelCount)
    {
        return "?";
    }
    return kAudioChannelNames[index];
}

bool ebeat::findAudioChannel(const std::string& name, AudioChannel& outChannel)
{
    for (int index = 0; index < kAudioChannelCount; ++index)
    {
        if (name == kAudioChannelNames[index])
        {
            outChannel = static_cast<AudioChannel>(index);
            return true;
        }
    }
    return false;
}

void AudioLevel::set(AudioChannel channel, float inLevel, double when)
{
    Reading& reading = readings[static_cast<int>(channel)];
    reading.level.store(std::clamp(inLevel, 0.0f, 1.0f));
    reading.stamp.store(when);
    reading.updates.fetch_add(1);
}

float AudioLevel::get(AudioChannel channel, double now) const
{
    const Reading& reading = readings[static_cast<int>(channel)];
    const double last = reading.stamp.load();
    if (last < 0.0)
    {
        return 0.0f;   // never fed
    }
    const double age = now - last;
    if (age <= kHoldFor)
    {
        return reading.level.load();
    }
    if (age >= kStaleAfter)
    {
        return 0.0f;
    }
    const double fade = (age - kHoldFor) / (kStaleAfter - kHoldFor);
    return reading.level.load() * static_cast<float>(1.0 - fade);
}

bool AudioLevel::isLive(AudioChannel channel, double now) const
{
    const double last = readings[static_cast<int>(channel)].stamp.load();
    return (last >= 0.0) && ((now - last) < kHoldFor);
}

unsigned long long AudioLevel::getUpdates(AudioChannel channel) const
{
    return readings[static_cast<int>(channel)].updates.load();
}

bool AudioLevel::isAnyLive(double now) const
{
    for (int index = 0; index < kAudioChannelCount; ++index)
    {
        if (isLive(static_cast<AudioChannel>(index), now))
        {
            return true;
        }
    }
    return false;
}

std::string AudioLevel::describe(double now) const
{
    char text[128];
    std::snprintf(text, sizeof(text), "avg=%.2f inst=%.2f meter=%.2f",
                  static_cast<double>(get(VuSource::Average, now)),
                  static_cast<double>(get(VuSource::Instant, now)),
                  static_cast<double>(get(VuSource::Meter, now)));
    return std::string(text);
}

std::string AudioLevel::describeLive(double now) const
{
    std::string text;
    for (int index = 0; index < kAudioChannelCount; ++index)
    {
        const AudioChannel channel = static_cast<AudioChannel>(index);
        if (!isLive(channel, now))
        {
            continue;
        }
        char one[48];
        std::snprintf(one, sizeof(one), "%s=%.2f",
                      audioChannelName(channel),
                      static_cast<double>(get(channel, now)));
        if (!text.empty())
        {
            text += ' ';
        }
        text += one;
    }
    return text.empty() ? std::string("none") : text;
}

void AudioLevelProperties::reflect(ecore::PropertyBag& bag)
{
    for (int index = 0; index < kAudioChannelCount; ++index)
    {
        const AudioChannel channel = static_cast<AudioChannel>(index);
        bag.add(audioChannelName(channel), values[index], 0.0f, 1.0f,
                [this, channel, index]() { bus.set(channel, values[index], when); });
    }
}
