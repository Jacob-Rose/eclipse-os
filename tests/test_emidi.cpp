// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <deque>

#include "ecore/property.h"
#include "emidi/binding.h"
#include "emidi/clock.h"
#include "emidi/message.h"
#include "emidi/parser.h"
#include "emidi/transport.h"

using namespace ecore;
using namespace emidi;

namespace
{
    struct Recorder : public MessageSink
    {
        std::vector<Message> seen;
        std::vector<double> whens;
        void onMessage(const Message& message, double when) override
        {
            seen.push_back(message);
            whens.push_back(when);
        }
    };

    struct QueueTransport : public Transport
    {
        std::deque<uint8_t> in;
        std::vector<uint8_t> out;
        int readByte() override
        {
            if (in.empty()) return -1;
            const uint8_t b = in.front(); in.pop_front(); return b;
        }
        bool write(const uint8_t* data, size_t length) override
        {
            out.insert(out.end(), data, data + length);
            return true;
        }
    };

    struct ClockRecorder : public ClockSink
    {
        std::vector<double> beats;
        std::vector<float> tempos;
        int restarts{0};
        void onBeat(double when) override { beats.push_back(when); }
        void onTempo(float bpm, double) override { tempos.push_back(bpm); }
        void onRestart(double) override { ++restarts; }
    };
}

TEST(emidi_message_reads_its_fields)
{
    const Message on(0x93, 60, 100);
    CHECK(on.isChannel());
    CHECK_EQ(on.channel(), 4);
    CHECK_EQ(on.kind(), status::NoteOn);
    CHECK(on.isNoteOn());
    CHECK(!on.isNoteOff());

    const Message offByVelocity(0x90, 60, 0);
    CHECK(offByVelocity.isNoteOff());
    CHECK(!offByVelocity.isNoteOn());

    const Message cc(0xB0, 7, 127);
    CHECK(cc.isControlChange());
    CHECK_EQ(cc.number(), 7);
    CHECK_EQ(cc.value(), 127);

    const Message clock(status::Clock);
    CHECK(clock.isRealtime());
    CHECK_EQ(clock.channel(), 0);

    const Message bend(0xE0, 0x00, 0x40);
    CHECK_EQ(bend.pitchBend(), 8192);
}

TEST(emidi_parser_frames_messages_with_running_status)
{
    Parser parser;
    Recorder sink;

    // note on, then two more notes by running status, then a program change
    const uint8_t stream[] = {0x90, 60, 100, 62, 100, 64, 0, 0xC0, 5};
    parser.feed(stream, sizeof(stream), 1.5, sink);

    CHECK_EQ(sink.seen.size(), 4u);
    CHECK_EQ(sink.seen[0].number(), 60);
    CHECK_EQ(sink.seen[1].number(), 62);
    CHECK_EQ(sink.seen[1].status, 0x90);
    CHECK(sink.seen[2].isNoteOff());
    CHECK_EQ(sink.seen[3].status, 0xC0);
    CHECK_EQ(sink.seen[3].data1, 5);
    CHECK_NEAR(sink.whens[0], 1.5, 0.0);
}

TEST(emidi_parser_passes_realtime_through_a_message_and_skips_sysex)
{
    Parser parser;
    Recorder sink;

    // a clock tick lands between a note's data bytes; then sysex with data
    // bytes nobody should see; then a song position pointer
    const uint8_t stream[] = {0x90, 60, 0xF8, 100, 0xF0, 0x7E, 0x01, 0xF7, 0xF2, 0x04, 0x00};
    parser.feed(stream, sizeof(stream), 0.0, sink);

    CHECK_EQ(sink.seen.size(), 3u);
    CHECK_EQ(sink.seen[0].status, status::Clock);
    CHECK(sink.seen[1].isNoteOn());
    CHECK_EQ(sink.seen[1].value(), 100);
    CHECK_EQ(sink.seen[2].status, status::SongPosition);
    CHECK_EQ(sink.seen[2].songPosition(), 4);

    // a stream joined mid-message has no status to lean on
    Parser late;
    Recorder none;
    const uint8_t tail[] = {100, 64, 100};
    late.feed(tail, sizeof(tail), 0.0, none);
    CHECK_EQ(none.seen.size(), 0u);
}

TEST(emidi_clock_counts_beats_and_measures_tempo)
{
    ClockFollower follower;
    ClockRecorder sink;

    // 120 bpm: a beat is 0.5s, a tick is 0.5 / 24
    const double tick = 0.5 / kClocksPerBeat;
    double when = 10.0;
    for (int i = 0; i < 3 * kClocksPerBeat; ++i)
    {
        follower.handle(Message(status::Clock), when, sink);
        when += tick;
    }

    CHECK_EQ(sink.beats.size(), 3u);
    CHECK_NEAR(sink.beats[0], 10.0, 1e-9);
    CHECK_NEAR(sink.beats[1], 10.5, 1e-9);
    CHECK(!sink.tempos.empty());
    CHECK_NEAR(sink.tempos.back(), 120.0f, 0.01f);
    CHECK_EQ(follower.getBeats(), 3u);

    // start resets the phase and the window; the next tick is a beat
    follower.handle(Message(status::Start), when, sink);
    CHECK_EQ(sink.restarts, 1);
    follower.handle(Message(status::Clock), when, sink);
    CHECK_EQ(sink.beats.size(), 4u);

    // song position: sixteenth 2 is tick 12 of the beat
    follower.handle(Message(status::SongPosition, 2, 0), when, sink);
    CHECK_EQ(follower.getTickInBeat(), 12);

    // a tap puts the next tick on the beat
    follower.alignToNow();
    CHECK_EQ(follower.getTickInBeat(), 0);

    // an impossible tempo is not reported
    ClockFollower strict;
    ClockRecorder quiet;
    strict.setBpmRange(60.0f, 200.0f);
    for (int i = 0; i < 2 * kClocksPerBeat; ++i)
    {
        strict.handle(Message(status::Clock), i * 1.0, quiet); // one tick a second: 2.5 bpm
    }
    CHECK(quiet.tempos.empty());
    CHECK_EQ(quiet.beats.size(), 2u);
}

namespace
{
    struct Look : public Reflectable
    {
        float gain{0.5f};
        bool bInverted{false};
        HSV tint;
        std::vector<std::string> fired;
        void reflect(PropertyBag& bag) override
        {
            bag.add("gain", gain, 0.0f, 2.0f);
            bag.add("inverted", bInverted);
            bag.add("tint", tint);
        }
    };
}

TEST(emidi_binding_lands_controls_on_properties_and_notes_on_triggers)
{
    Look look;
    Binding binding;
    binding.bindControl(1, 7, "gain");
    binding.bindControl(0, 8, "inverted");   // any channel
    binding.bindControl(1, 9, "tint");       // a colour: refused
    binding.bindNote(1, 60, GameplayTag("look.hit"));
    binding.trigger = [&](const GameplayTag& tag) { look.fired.push_back(tag.getText()); return true; };

    CHECK(binding.apply(Message(0xB0, 7, 127), look));
    CHECK_NEAR(look.gain, 2.0f, 1e-6f);
    CHECK(binding.apply(Message(0xB0, 7, 0), look));
    CHECK_NEAR(look.gain, 0.0f, 1e-6f);

    CHECK(!binding.apply(Message(0xB1, 7, 64), look));     // wrong channel
    CHECK(binding.apply(Message(0xB5, 8, 100), look));     // any channel
    CHECK(look.bInverted);
    CHECK(binding.apply(Message(0xB5, 8, 10), look));
    CHECK(!look.bInverted);

    CHECK(!binding.apply(Message(0xB0, 9, 100), look));    // colour is not one number

    CHECK(binding.apply(Message(0x90, 60, 90), look));
    CHECK_EQ(look.fired.size(), 1u);
    CHECK_EQ(look.fired[0], std::string("look.hit"));
    CHECK(!binding.apply(Message(0x90, 61, 90), look));    // unbound note
    CHECK(!binding.apply(Message(0x90, 60, 0), look));     // a note off is not a hit
    CHECK(!binding.apply(Message(status::Clock), look));

    binding.clear();
    CHECK(!binding.apply(Message(0xB0, 7, 127), look));
}

TEST(emidi_port_pumps_a_transport_and_sends_back)
{
    QueueTransport wire;
    MidiPort port;
    Recorder sink;
    port.setTransport(&wire);

    wire.in = {0x90, 60, 100, 0xF8};
    port.tick(3.0, sink);
    CHECK_EQ(sink.seen.size(), 2u);
    CHECK_EQ(port.getBytesRead(), 4u);
    CHECK_NEAR(sink.whens[0], 3.0, 0.0);

    CHECK(port.send(Message(0x90, 60, 100)));
    CHECK(port.send(Message(0xC0, 3)));
    CHECK(port.send(Message(status::Clock)));
    CHECK_EQ(wire.out.size(), 3u + 2u + 1u);
    CHECK_EQ(wire.out[3], 0xC0);
    CHECK_EQ(wire.out[5], 0xF8);

    MidiPort unplugged;
    unplugged.tick(0.0, sink);
    CHECK(!unplugged.send(Message(status::Clock)));
}
