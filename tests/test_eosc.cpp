// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <cstring>
#include <deque>

#include <ecore/property.h>
#include <ecore/reflectable.h>
#include <enet/datagram.h>
#include <eosc/address.h>
#include <eosc/binding.h>
#include <eosc/packet.h>
#include <eosc/transport.h>

using namespace eosc;

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
}

TEST(eosc_packets_round_trip_every_type_and_pad_to_four)
{
    Message out("/controls/global/color/1");
    out.add(1.0f).add(0.5f).add(0.0f).add(int32_t(7)).add("scene").add(true).add(false)
       .add(Arg::Double(2.5)).add(Arg::Blob(std::string("\x01\x02\x03", 3))).add(Arg::NilValue());

    const std::vector<uint8_t> bytes = Packet::encode(out);
    CHECK_EQ(bytes.size() % 4, 0u);
    CHECK_EQ(bytes.size(), Packet::encodedSize(out));

    Message in;
    CHECK(Packet::decodeOne(bytes.data(), bytes.size(), in));
    CHECK_EQ(in.address, out.address);
    CHECK_EQ(in.args.size(), 10u);
    CHECK_EQ(in.typeTags(), std::string(",fffisTFdbN"));
    CHECK_NEAR(in.args[1].number(), 0.5f, 0.0f);
    CHECK_EQ(in.args[3].i, 7);
    CHECK_EQ(in.args[4].bytes, std::string("scene"));
    CHECK(in.args[5].type == Arg::Type::True);
    CHECK_NEAR(in.args[7].d, 2.5, 0.0);
    CHECK_EQ(in.args[8].bytes.size(), 3u);
    CHECK(!in.args[9].isNumber());

    // the python encoder's exact bytes for a bare address
    const std::vector<uint8_t> bang = Packet::encode(Message("/bang"));
    CHECK_EQ(bang.size(), 12u);
    CHECK_EQ(std::memcmp(bang.data(), "/bang\0\0\0,\0\0\0", 12), 0);

    // too small a buffer is a refusal, not a partial packet
    uint8_t tiny[8];
    CHECK_EQ(Packet::encode(out, tiny, sizeof(tiny)), 0u);
}

TEST(eosc_decode_unpacks_bundles_and_survives_garbage)
{
    const std::vector<uint8_t> a = Packet::encode(Message("/a").add(1.0f));
    const std::vector<uint8_t> b = Packet::encode(Message("/b").add("x"));

    std::vector<uint8_t> bundle;
    const char header[16] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
    bundle.insert(bundle.end(), header, header + 16);
    for (const std::vector<uint8_t>* part : {&a, &b})
    {
        const uint32_t size = static_cast<uint32_t>(part->size());
        bundle.push_back(static_cast<uint8_t>(size >> 24));
        bundle.push_back(static_cast<uint8_t>(size >> 16));
        bundle.push_back(static_cast<uint8_t>(size >> 8));
        bundle.push_back(static_cast<uint8_t>(size));
        bundle.insert(bundle.end(), part->begin(), part->end());
    }

    Recorder sink;
    CHECK_EQ(Packet::decode(bundle.data(), bundle.size(), sink, 4.0), 2);
    CHECK_EQ(sink.seen.size(), 2u);
    CHECK_EQ(sink.seen[1].address, std::string("/b"));
    CHECK_NEAR(sink.whens[0], 4.0, 0.0);

    // not an address, a truncated argument list, an unknown tag
    const uint8_t noSlash[] = "nope\0\0\0\0,f\0\0";
    CHECK_EQ(Packet::decode(noSlash, sizeof(noSlash) - 1, sink, 0.0), 0);
    const uint8_t cut[] = "/x\0\0,ff\0\0\0\0";   // one float promised twice, none sent
    Recorder partial;
    CHECK_EQ(Packet::decode(cut, sizeof(cut) - 1, partial, 0.0), 1);
    CHECK_EQ(partial.seen[0].args.size(), 0u);
    const uint8_t odd[] = "/x\0\0,iq\0\0\0\0\x05";
    Recorder known;
    Packet::decode(odd, sizeof(odd) - 1, known, 0.0);
    CHECK_EQ(known.seen[0].args.size(), 1u);
    CHECK_EQ(Packet::decode(nullptr, 0, sink, 0.0), 0);
}

TEST(eosc_addresses_match_like_a_glob)
{
    CHECK(matchAddress("*basslevel*", "/syn/audio/BassLevel"));
    CHECK(matchAddress("*BassLevel*", "/syn_basslevel"));
    CHECK(!matchAddress("*basslevel*", "/syn/audio/MidLevel"));
    CHECK(matchAddress("/syn/?it?", "/syn/Hits"));
    CHECK(!matchAddress("/syn/?it?", "/syn/Hit"));
    CHECK(matchAddress("/scenes/[a-c]*", "/scenes/Bloom"));
    CHECK(!matchAddress("/scenes/[!a-c]*", "/scenes/Bloom"));
    CHECK(matchAddress("/syn/{bass,mid}level", "/syn/MidLevel"));
    CHECK(!matchAddress("/syn/{bass,mid}level", "/syn/HighLevel"));
    CHECK(matchAddress("/exact", "/EXACT"));
    CHECK(!matchAddress("/exact", "/EXACT", false));
    CHECK(matchAddress("*", ""));
    CHECK(!matchAddress("/a", "/a/b"));
}

TEST(eosc_bindings_scale_limit_and_edge_trigger)
{
    Binding level;
    level.pattern = "*level*";
    level.exclude = {"*bass*", "*mid*"};
    level.minInterval = 0.1;
    level.minChange = 0.05;
    level.maxInterval = 0.5;
    level.low = 0.0f;
    level.high = 2.0f;

    float value = 0.0f;
    CHECK(level.fires(Message("/syn/Level").add(1.0f), 1.0, value));
    CHECK_NEAR(value, 0.5f, 1e-6f);                                   // scaled across 0..2
    CHECK(!level.fires(Message("/syn/Level").add(1.9f), 1.05, value)); // inside the interval
    CHECK(!level.fires(Message("/syn/Level").add(1.02f), 1.2, value)); // too small a change
    CHECK(level.fires(Message("/syn/Level").add(1.02f), 1.6, value));  // keepalive restates it
    CHECK(!level.fires(Message("/syn/BassLevel").add(1.0f), 5.0, value));   // vetoed
    CHECK(!level.fires(Message("/syn/Level").add("loud"), 5.0, value));     // not a number
    CHECK(!level.fires(Message("/syn/Level"), 5.0, value));                 // no argument
    level.enabled = false;
    CHECK(!level.fires(Message("/syn/Level").add(2.0f), 9.0, value));

    Binding beat;
    beat.pattern = "*onbeat*";
    beat.mode = Binding::Mode::Trigger;
    beat.threshold = 0.5f;
    CHECK(!beat.fires(Message("/syn/OnBeat").add(0.2f), 0.0, value));
    CHECK(beat.fires(Message("/syn/OnBeat").add(0.9f), 0.01, value));    // the rising edge
    CHECK_NEAR(value, 1.0f, 0.0f);
    CHECK(!beat.fires(Message("/syn/OnBeat").add(0.95f), 0.02, value));  // still up: not again
    CHECK(!beat.fires(Message("/syn/OnBeat").add(0.1f), 0.03, value));
    CHECK(beat.fires(Message("/syn/OnBeat").add(true), 0.04, value));     // a bool is a number

    Binding::Mode mode;
    CHECK(Binding::parseMode("trigger", mode) && mode == Binding::Mode::Trigger);
    CHECK(!Binding::parseMode("edge", mode));
    CHECK_EQ(std::string(Binding::modeName(Binding::Mode::Value)), std::string("value"));
}

namespace
{
    struct Look : public ecore::Reflectable
    {
        float gain{0.0f};
        float width{0.0f};
        void reflect(ecore::PropertyBag& bag) override
        {
            bag.add("gain", gain, 0.0f, 1.0f);
            bag.add("width", width, 0.0f, 4.0f);
        }
    };
}

TEST(eosc_binding_set_runs_a_map_and_lands_on_properties)
{
    BindingSet map;
    Binding bass;
    bass.label = "bass";
    bass.pattern = "*bass*";
    bass.property = "gain";
    bass.minInterval = 0.0;
    bass.minChange = 0.0;
    map.add(bass);
    Binding hits;
    hits.label = "hits";
    hits.pattern = "*hits*";
    hits.mode = Binding::Mode::Trigger;
    map.add(hits);
    CHECK_EQ(map.size(), 2u);
    CHECK(map.find("hits") != nullptr);
    CHECK(map.find("nope") == nullptr);

    std::vector<std::string> fired;
    map.handle(Message("/syn/BassLevel").add(0.75f), 1.0, [&](const Binding& binding, float value)
    {
        fired.push_back(binding.label + "=" + std::to_string(value));
    });
    CHECK_EQ(fired.size(), 1u);
    CHECK_EQ(fired[0].substr(0, 9), std::string("bass=0.75"));

    Look look;
    CHECK_EQ(map.apply(Message("/syn/BassLevel").add(0.25f), 2.0, look), 1);
    CHECK_NEAR(look.gain, 0.25f, 1e-6f);
    CHECK_EQ(map.apply(Message("/syn/Hits").add(1.0f), 2.1, look), 0);   // no property, no tag: apply leaves it alone
    CHECK_EQ(map.handle(Message("/syn/Hits").add(1.0f), 2.2, nullptr), 1); // so handle still sees the edge
    CHECK_EQ(map.handle(Message("/syn/Hits").add(1.0f), 2.3, nullptr), 0);

    // a value lands in the property's own range; a tag fires the trigger
    Binding wide;
    wide.label = "wide";
    wide.pattern = "*mid*";
    wide.property = "width";
    wide.minInterval = 0.0;
    map.add(wide);
    Binding kick;
    kick.label = "kick";
    kick.pattern = "*kick*";
    kick.mode = Binding::Mode::Trigger;
    kick.tag = ecore::GameplayTag("look.kick");
    map.add(kick);
    std::vector<std::string> triggered;
    map.trigger = [&](const ecore::GameplayTag& tag) { triggered.push_back(tag.getText()); return true; };
    CHECK_EQ(map.apply(Message("/syn/MidLevel").add(0.5f), 3.0, look), 1);
    CHECK_NEAR(look.width, 2.0f, 1e-6f);
    CHECK_EQ(map.apply(Message("/syn/Kick").add(1.0f), 3.1, look), 1);
    CHECK_EQ(triggered.size(), 1u);
    CHECK_EQ(triggered[0], std::string("look.kick"));
}

namespace
{
    struct QueueTransport : public enet::DatagramTransport
    {
        std::deque<std::vector<uint8_t>> in;
        std::vector<std::vector<uint8_t>> out;
        int receive(uint8_t* buffer, size_t capacity) override
        {
            if (in.empty()) return 0;
            std::vector<uint8_t> packet = in.front();
            in.pop_front();
            if (packet.size() > capacity) return -1;
            std::memcpy(buffer, packet.data(), packet.size());
            return static_cast<int>(packet.size());
        }
        bool send(const uint8_t* data, size_t length) override
        {
            out.emplace_back(data, data + length);
            return true;
        }
    };
}

TEST(eosc_port_pumps_a_transport_and_sends_back)
{
    QueueTransport wire;
    OscPort port;
    port.setTransport(&wire);
    Recorder sink;

    wire.in.push_back(Packet::encode(Message("/a").add(1.0f)));
    wire.in.push_back(std::vector<uint8_t>(12, 'x'));          // junk
    wire.in.push_back(std::vector<uint8_t>(OscPort::kDatagramCapacity + 1, 0));
    CHECK_EQ(port.tick(3.0, sink), 1);
    CHECK_EQ(sink.seen.size(), 1u);
    CHECK_EQ(port.getPackets(), 2u);
    CHECK_EQ(port.getUndecodable(), 1u);
    CHECK_EQ(port.getOversized(), 1u);

    CHECK(port.send(Message("/lamp").add(int32_t(3))));
    CHECK_EQ(wire.out.size(), 1u);
    CHECK_EQ(port.getSent(), 1u);
    CHECK(!port.send(Message("")));   // no address: nothing to send
    CHECK_EQ(port.getDropped(), 1u);

    OscPort unplugged;
    CHECK_EQ(unplugged.tick(0.0, sink), 0);
    CHECK(!unplugged.send(Message("/x")));
}
