// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <emqtt/ha_discovery.h>
#include <emqtt/mqtt_client.h>
#include <emqtt/mqtt_handler.h>
#include <emqtt/mqtt_packet.h>
#include <emqtt/mqtt_transport_host.h>

#include <functional>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

using namespace emqtt;

namespace
{
    using Bytes = std::vector<uint8_t>;

    /// The IMqttTransport a test scripts: says what it is told and records the rest.
    struct FakeTransport : public IMqttTransport
    {
        struct Shared
        {
            bool connectable{true};
            bool connected{false};
            int connectAttempts{0};
            std::vector<std::string> published;
            std::vector<std::string> subscribed;
            MqttMessageCallback callback;
        };

        explicit FakeTransport(Shared& inShared) : shared(inShared) {}

        bool init(const char*, uint16_t, const char*, const char*, const char*) override { return true; }
        bool connect() override
        {
            ++shared.connectAttempts;
            shared.connected = shared.connectable;
            return shared.connected;
        }
        void disconnect() override { shared.connected = false; }
        bool isConnected() override { return shared.connected; }
        bool publish(const char* topic, const char* payload, bool) override
        {
            shared.published.push_back(std::string(topic) + "=" + payload);
            return true;
        }
        bool subscribe(const char* topic, uint8_t) override
        {
            shared.subscribed.push_back(topic);
            return true;
        }
        void loop() override {}
        void setCallback(MqttMessageCallback callback) override { shared.callback = callback; }
        void setKeepAlive(uint16_t) override {}

        Shared& shared;
    };

    void sleepMs(int milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    int elapsedMs(std::chrono::steady_clock::time_point since)
    {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - since).count());
    }
}

TEST(mqtt_encodes_a_connect_with_credentials)
{
    const Bytes expected = {
        0x10, 22,
        0, 4, 'M', 'Q', 'T', 'T', 4, 0xC2, 0, 15,
        0, 2, 'i', 'd',
        0, 1, 'u',
        0, 3, 'p', 'w', 'd',
    };
    CHECK(encodeConnect("id", "u", "pwd", 15) == expected);
}

TEST(mqtt_encodes_a_connect_without_credentials)
{
    const Bytes expected = {
        0x10, 14,
        0, 4, 'M', 'Q', 'T', 'T', 4, 0x02, 0, 15,
        0, 2, 'i', 'd',
    };
    CHECK(encodeConnect("id", nullptr, nullptr, 15) == expected);
}

TEST(mqtt_encodes_publish_subscribe_and_the_small_packets)
{
    CHECK(encodePublish("a/b", "hi", false) == (Bytes{0x30, 7, 0, 3, 'a', '/', 'b', 'h', 'i'}));
    CHECK(encodePublish("a", "", true) == (Bytes{0x31, 3, 0, 1, 'a'}));
    CHECK(encodeSubscribe(10, "a", 1) == (Bytes{0x82, 6, 0, 10, 0, 1, 'a', 1}));
    CHECK(encodePubAck(258) == (Bytes{0x40, 2, 1, 2}));
    CHECK(encodePingReq() == (Bytes{0xC0, 0}));
    CHECK(encodeDisconnect() == (Bytes{0xE0, 0}));
}

TEST(mqtt_a_long_packet_takes_a_two_byte_length_and_survives_a_trickle)
{
    const std::string payload(300, 'x');
    const Bytes wire = encodePublish("t", payload.c_str(), false);
    CHECK_EQ(wire[1], 0x80 | ((303) % 128));
    CHECK_EQ(wire[2], 303 / 128);

    MqttPacketReader reader;
    MqttPacket packet;
    for (size_t i = 0; i + 1 < wire.size(); ++i)
    {
        reader.feed(&wire[i], 1);
        CHECK(!reader.next(packet));
    }
    reader.feed(&wire.back(), 1);
    CHECK(reader.next(packet));

    MqttPublish message;
    CHECK(parsePublish(packet, message));
    CHECK(message.topic == "t");
    CHECK_EQ(message.length, payload.size());
}

TEST(mqtt_reader_takes_two_packets_from_one_read)
{
    Bytes wire = encodePingReq();
    const Bytes second = encodePublish("t", "p", false);
    wire.insert(wire.end(), second.begin(), second.end());

    MqttPacketReader reader;
    reader.feed(wire.data(), wire.size());
    MqttPacket packet;
    CHECK(reader.next(packet));
    CHECK(packet.type == MqttPacketType::PingReq);
    CHECK(reader.next(packet));
    CHECK(packet.type == MqttPacketType::Publish);
    CHECK(!reader.next(packet));
}

TEST(mqtt_reader_fails_on_a_length_no_packet_can_have)
{
    const Bytes tooManyDigits = {0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    MqttPacketReader reader;
    reader.feed(tooManyDigits.data(), tooManyDigits.size());
    MqttPacket packet;
    CHECK(!reader.next(packet));
    CHECK(reader.failed());

    const Bytes tooBig = {0x30, 0xFF, 0xFF, 0x7F};
    MqttPacketReader big;
    big.feed(tooBig.data(), tooBig.size());
    CHECK(!big.next(packet));
    CHECK(big.failed());
}

TEST(mqtt_parse_publish_reads_a_qos1_packet_id_and_refuses_a_short_one)
{
    MqttPacket packet;
    packet.type = MqttPacketType::Publish;
    packet.flags = 0x02;
    packet.body = {0, 1, 't', 1, 2, 'p'};

    MqttPublish message;
    CHECK(parsePublish(packet, message));
    CHECK_EQ(message.qos, 1);
    CHECK_EQ(message.packetId, 258);
    CHECK_EQ(message.length, 1u);

    packet.body = {0, 9, 't'};
    CHECK(!parsePublish(packet, message));
}

TEST(mqtt_client_backs_off_while_the_broker_is_away_and_recovers)
{
    FakeTransport::Shared shared;
    shared.connectable = false;
    MqttClient client(std::make_unique<FakeTransport>(shared));
    client.init(MqttConfig("broker", "id"));
    client.setReconnectInterval(0.05f);

    client.tick(0.0f);
    CHECK_EQ(shared.connectAttempts, 1);

    for (int i = 0; i < 100; ++i)
    {
        client.tick(0.03f);     // dt does not drive the retry: only real time does
    }
    CHECK_EQ(shared.connectAttempts, 1);

    sleepMs(70);
    client.tick(0.0f);
    CHECK_EQ(shared.connectAttempts, 2);

    shared.connectable = true;
    sleepMs(120);
    client.tick(0.0f);
    CHECK_EQ(shared.connectAttempts, 3);
    CHECK(client.isConnected());
}

TEST(mqtt_handler_routes_a_message_to_its_topic)
{
    MqttHandler handler;
    std::string heard;
    handler.registerHandler("a/b", [&heard](const std::string& payload) { heard = payload; });

    uint8_t payload[] = {'o', 'n'};
    handler.handleMessage("a/b", payload, 2);
    CHECK(heard == "on");
}

TEST(mqtt_payload_parser_reads_home_assistants_rgb)
{
    uint8_t red = 0, green = 0, blue = 0;
    CHECK(PayloadParser::parseRGB("255,128,0", red, green, blue));
    CHECK_EQ(red, 255);
    CHECK_EQ(green, 128);
    CHECK_EQ(blue, 0);
    CHECK(!PayloadParser::parseRGB("255,128", red, green, blue));
    CHECK(!PayloadParser::parseRGB("256,0,0", red, green, blue));
}

TEST(mqtt_ha_discovery_publishes_off_arduino)
{
    FakeTransport::Shared shared;
    MqttClient client(std::make_unique<FakeTransport>(shared));
    client.init(MqttConfig("broker", "id"));
    CHECK(client.connect());

    HomeAssistantConfig config;
    config.enabled = true;
    config.deviceName = "Desk";
    config.deviceId = "desk_01";

    HomeAssistantDiscovery discovery(client);
    discovery.init(config);
    CHECK(discovery.publishLightDiscovery("desk_light", "Desk Light", {"noise"}));
    CHECK_EQ(shared.published.size(), 1u);
    CHECK(shared.published[0].rfind("homeassistant/light/", 0) == 0);
}

#if !defined(_WIN32)

namespace
{
    /// A broker on loopback, as much of one as a test needs. `answer` is what
    /// it does with a CONNECT.
    class LoopbackBroker
    {
    public:
        enum class Answer { Accept, Refuse, Silent };

        explicit LoopbackBroker(Answer inAnswer) : answer(inAnswer)
        {
            listener = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            ::listen(listener, 1);
            socklen_t length = sizeof(address);
            ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
            port = ntohs(address.sin_port);
            worker = std::thread([this] { run(); });
        }

        ~LoopbackBroker()
        {
            running = false;
            worker.join();
            ::close(listener);
        }

        uint16_t getPort() const { return port; }

        /// Sends `bytes` to the connected client from the broker's thread.
        void say(Bytes bytes)
        {
            pendingBytes.store(new Bytes(std::move(bytes)));
        }

        /// Hangs up on the client.
        void hangUp() { hangingUp = true; }

        std::atomic<int> connects{0};
        std::atomic<int> pings{0};
        std::atomic<int> pubAcks{0};
        std::atomic<int> subscribes{0};
        std::atomic<int> publishes{0};

    private:
        void run()
        {
            int client = -1;
            MqttPacketReader reader;
            while (running)
            {
                pollfd entry{client >= 0 ? client : listener, POLLIN, 0};
                const bool ready = ::poll(&entry, 1, 5) > 0;

                if (client < 0)
                {
                    if (ready)
                    {
                        client = ::accept(listener, nullptr, nullptr);
                    }
                    continue;
                }

                if (Bytes* bytes = pendingBytes.exchange(nullptr))
                {
                    ::send(client, bytes->data(), bytes->size(), 0);
                    delete bytes;
                }
                if (hangingUp.exchange(false))
                {
                    ::close(client);
                    client = -1;
                    continue;
                }
                if (!ready)
                {
                    continue;
                }

                uint8_t bytes[2048];
                const auto count = ::recv(client, bytes, sizeof(bytes), 0);
                if (count <= 0)
                {
                    ::close(client);
                    client = -1;
                    continue;
                }
                reader.feed(bytes, static_cast<size_t>(count));

                MqttPacket packet;
                while (reader.next(packet))
                {
                    handle(client, packet);
                }
            }
            if (client >= 0)
            {
                ::close(client);
            }
        }

        void handle(int client, const MqttPacket& packet)
        {
            switch (packet.type)
            {
                case MqttPacketType::Connect:
                {
                    ++connects;
                    if (answer == Answer::Silent)
                    {
                        break;
                    }
                    const uint8_t code = answer == Answer::Accept ? 0 : 5;
                    const uint8_t connAck[] = {0x20, 2, 0, code};
                    ::send(client, connAck, sizeof(connAck), 0);
                    break;
                }
                case MqttPacketType::PingReq:
                {
                    ++pings;
                    const uint8_t pingResp[] = {0xD0, 0};
                    ::send(client, pingResp, sizeof(pingResp), 0);
                    break;
                }
                case MqttPacketType::Subscribe: ++subscribes; break;
                case MqttPacketType::Publish:   ++publishes; break;
                case MqttPacketType::PubAck:    ++pubAcks; break;
                default: break;
            }
        }

        Answer answer;
        int listener{-1};
        uint16_t port{0};
        std::thread worker;
        std::atomic<bool> running{true};
        std::atomic<bool> hangingUp{false};
        std::atomic<Bytes*> pendingBytes{nullptr};
    };

    bool pumpUntil(MqttTransportHost& transport, const std::function<bool()>& done, int milliseconds = 2000)
    {
        const auto started = std::chrono::steady_clock::now();
        while (elapsedMs(started) < milliseconds)
        {
            transport.loop();
            if (done())
            {
                return true;
            }
            sleepMs(2);
        }
        return false;
    }
}

TEST(mqtt_host_transport_talks_to_a_broker_both_ways)
{
    LoopbackBroker broker(LoopbackBroker::Answer::Accept);
    MqttTransportHost transport;
    transport.init("127.0.0.1", broker.getPort(), "test");

    std::string topic;
    std::string payload;
    transport.setCallback([&](const char* inTopic, uint8_t* inPayload, unsigned int length) {
        topic = inTopic;
        payload.assign(reinterpret_cast<const char*>(inPayload), length);
    });

    CHECK(transport.connect());
    CHECK(transport.isConnected());
    CHECK(transport.subscribe("a/b"));
    CHECK(transport.publish("c/d", "out"));
    CHECK(pumpUntil(transport, [&] { return broker.subscribes == 1 && broker.publishes == 1; }));

    broker.say(encodePublish("a/b", "in", false));
    CHECK(pumpUntil(transport, [&] { return !payload.empty(); }));
    CHECK(topic == "a/b");
    CHECK(payload == "in");
}

TEST(mqtt_host_transport_acknowledges_a_qos1_message)
{
    LoopbackBroker broker(LoopbackBroker::Answer::Accept);
    MqttTransportHost transport;
    transport.init("127.0.0.1", broker.getPort(), "test");
    CHECK(transport.connect());

    broker.say(Bytes{0x32, 6, 0, 1, 't', 0, 7, 'p'});
    CHECK(pumpUntil(transport, [&] { return broker.pubAcks == 1; }));
}

TEST(mqtt_host_transport_is_told_no)
{
    LoopbackBroker broker(LoopbackBroker::Answer::Refuse);
    MqttTransportHost transport;
    transport.init("127.0.0.1", broker.getPort(), "test", "user", "wrong");
    CHECK(!transport.connect());
    CHECK(!transport.isConnected());
}

TEST(mqtt_host_transport_gives_up_on_a_broker_that_never_answers)
{
    LoopbackBroker broker(LoopbackBroker::Answer::Silent);
    MqttTransportHost transport;
    transport.setTimeout(200);
    transport.init("127.0.0.1", broker.getPort(), "test");

    const auto started = std::chrono::steady_clock::now();
    CHECK(!transport.connect());
    const int spent = elapsedMs(started);
    CHECK(spent >= 150);
    CHECK(spent < 1000);
    CHECK_EQ(broker.connects.load(), 1);
}

TEST(mqtt_host_transport_fails_fast_when_nothing_is_listening)
{
    uint16_t freePort = 0;
    {
        LoopbackBroker gone(LoopbackBroker::Answer::Accept);
        freePort = gone.getPort();
    }
    MqttTransportHost transport;
    transport.setTimeout(200);
    transport.init("127.0.0.1", freePort, "test");

    const auto started = std::chrono::steady_clock::now();
    CHECK(!transport.connect());
    CHECK(elapsedMs(started) < 1000);
}

TEST(mqtt_host_transport_loop_never_waits)
{
    LoopbackBroker broker(LoopbackBroker::Answer::Accept);
    MqttTransportHost transport;
    transport.init("127.0.0.1", broker.getPort(), "test");
    CHECK(transport.connect());

    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
    {
        transport.loop();
    }
    CHECK(elapsedMs(started) < 200);
}

TEST(mqtt_host_transport_notices_a_broker_hanging_up_and_reconnects)
{
    LoopbackBroker broker(LoopbackBroker::Answer::Accept);
    MqttTransportHost transport;
    transport.init("127.0.0.1", broker.getPort(), "test");
    CHECK(transport.connect());

    broker.hangUp();
    CHECK(pumpUntil(transport, [&] { return !transport.isConnected(); }));

    CHECK(transport.connect());
    CHECK_EQ(broker.connects.load(), 2);
}

TEST(mqtt_host_transport_pings_when_idle)
{
    LoopbackBroker broker(LoopbackBroker::Answer::Accept);
    MqttTransportHost transport;
    transport.setKeepAlive(1);
    transport.init("127.0.0.1", broker.getPort(), "test");
    CHECK(transport.connect());

    CHECK(pumpUntil(transport, [&] { return broker.pings == 1; }, 2500));
    CHECK(transport.isConnected());
}

TEST(mqtt_client_runs_over_the_host_transport)
{
    LoopbackBroker broker(LoopbackBroker::Answer::Accept);
    MqttClient client(std::make_unique<MqttTransportHost>());
    const std::string host = "127.0.0.1";
    MqttConfig config(host.c_str(), "test");
    config.port = broker.getPort();
    client.init(config);

    client.tick(0.0f);
    CHECK(client.isConnected());
    CHECK(client.publish("a", "b"));
    for (int i = 0; i < 200 && broker.publishes == 0; ++i)
    {
        client.tick(0.0f);
        sleepMs(2);
    }
    CHECK_EQ(broker.publishes.load(), 1);
}

#endif
