// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <cstring>

#include <enet/host_udp.h>
#include <eosc/message.h>
#include <eosc/transport.h>

using namespace enet;

TEST(enet_host_udp_sends_a_datagram_to_itself_over_loopback)
{
    std::string error;
    HostUdp in;
    CHECK(in.listen("127.0.0.1", 0, error));
    CHECK(in.getLocalPort() > 0);
    in.setTimeout(500);

    HostUdp out;
    CHECK(out.point("127.0.0.1", in.getLocalPort(), error));
    CHECK_EQ(out.getRemoteAddress(), std::string("127.0.0.1:") + std::to_string(in.getLocalPort()));

    const uint8_t hello[] = "hello";
    CHECK(out.send(hello, sizeof(hello)));

    uint8_t buffer[64];
    const int got = in.receive(buffer, sizeof(buffer));
    CHECK_EQ(got, static_cast<int>(sizeof(hello)));
    CHECK(got == static_cast<int>(sizeof(hello)) && std::memcmp(buffer, hello, sizeof(hello)) == 0);

    // nothing more: the timeout returns empty rather than blocking forever
    in.setTimeout(10);
    CHECK_EQ(in.receive(buffer, sizeof(buffer)), 0);

    // a socket that only listens does not send; a closed one does nothing
    CHECK(!in.send(hello, sizeof(hello)));
    out.close();
    CHECK(!out.isOpen());
    CHECK(!out.send(hello, sizeof(hello)));

    CHECK(HostUdp::isLiteral("127.0.0.1"));
    CHECK(HostUdp::isLiteral("::1"));
    CHECK(!HostUdp::isLiteral("mac-mini.local"));
    std::string address;
    CHECK(HostUdp::resolve("127.0.0.1", 6000, address, error));
    CHECK_EQ(address, std::string("127.0.0.1:6000"));
    CHECK(!HostUdp::resolve("no.such.host.invalid", 6000, address, error));
    CHECK(!error.empty());
}

namespace
{
    struct Recorder : public eosc::MessageSink
    {
        std::vector<eosc::Message> seen;
        void onMessage(const eosc::Message& message, double) override { seen.push_back(message); }
    };
}

TEST(enet_host_udp_carries_osc_end_to_end)
{
    std::string error;
    HostUdp in;
    CHECK(in.listen("127.0.0.1", 0, error));
    in.setTimeout(500);
    HostUdp out;
    CHECK(out.point("localhost", in.getLocalPort(), error));

    eosc::OscPort sender;
    sender.setTransport(&out);
    CHECK(sender.send(eosc::Message("/syn/BassLevel").add(0.75f)));

    eosc::OscPort receiver;
    receiver.setTransport(&in);
    Recorder sink;
    CHECK_EQ(receiver.tick(1.0, sink), 1);
    CHECK_EQ(sink.seen.size(), 1u);
    if (!sink.seen.empty())
    {
        CHECK_EQ(sink.seen[0].address, std::string("/syn/BassLevel"));
        float level = 0.0f;
        CHECK(sink.seen[0].number(0, level));
        CHECK_NEAR(level, 0.75f, 1e-6f);
    }
}
