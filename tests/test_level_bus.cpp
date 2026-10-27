// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <ebeat/level_bus.h>
#include <ecore/property.h>
#include <eosc/binding.h>
#include <eosc/message.h>

using namespace ebeat;

TEST(level_bus_every_channel_names_itself_and_back)
{
    for (int index = 0; index < kAudioChannelCount; ++index)
    {
        const AudioChannel channel = static_cast<AudioChannel>(index);
        AudioChannel found = AudioChannel::Count;
        CHECK(findAudioChannel(audioChannelName(channel), found));
        CHECK(found == channel);
    }
    AudioChannel sub = AudioChannel::Count;
    CHECK(findAudioChannel("sub", sub));
    CHECK(sub == AudioChannel::Sub);
    CHECK(!findAudioChannel("subwoofer", sub));
}

TEST(level_bus_holds_a_reading_then_fades_it_out)
{
    AudioLevel bus;
    bus.set(AudioChannel::Sub, 0.8f, 10.0);
    CHECK_NEAR(bus.get(AudioChannel::Sub, 10.1), 0.8f, 1e-6f);
    CHECK(bus.isLive(AudioChannel::Sub, 10.1));
    CHECK_NEAR(bus.get(AudioChannel::Bass, 10.1), 0.0f, 1e-6f);   // its neighbour is untouched
    CHECK(!bus.isLive(AudioChannel::Sub, 10.0 + AudioLevel::kHoldFor + 0.01));
    CHECK_NEAR(bus.get(AudioChannel::Sub, 10.0 + AudioLevel::kStaleAfter + 0.01), 0.0f, 1e-6f);
}

TEST(level_bus_takes_a_binding_like_a_knob)
{
    AudioLevel bus;
    AudioLevelProperties levels(bus);
    ecore::PropertyBag bag;
    bag.reflect("audio", levels);
    CHECK(bag.find("audio.sub") != nullptr);
    CHECK(bag.find("audio.intensity") != nullptr);

    eosc::BindingSet map;
    eosc::Binding sub;
    sub.label = "mic sub";
    sub.pattern = "*/mic/sub";
    sub.property = "audio.sub";
    sub.minInterval = 0.0;
    map.add(sub);

    levels.when = 5.0;
    CHECK_EQ(map.apply(eosc::Message("/swim/mic/sub").add(0.6f), levels.when, bag), 1);
    CHECK_NEAR(bus.get(AudioChannel::Sub, 5.1), 0.6f, 1e-6f);
    CHECK_NEAR(bus.get(AudioChannel::Sub, 5.0 + AudioLevel::kStaleAfter + 0.01), 0.0f, 1e-6f);   // stamped when it arrived
    CHECK_EQ(map.apply(eosc::Message("/swim/mic/bass").add(0.6f), 5.2, bag), 0);
}
