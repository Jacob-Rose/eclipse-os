// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace emqtt
{
    /* @brief The MQTT 3.1.1 packet types a client sends or hears. */
    enum class MqttPacketType : uint8_t
    {
        Connect = 1,
        ConnAck = 2,
        Publish = 3,
        PubAck = 4,
        Subscribe = 8,
        SubAck = 9,
        PingReq = 12,
        PingResp = 13,
        Disconnect = 14
    };

    /* @brief One packet off the wire, its fixed header taken apart. */
    struct MqttPacket
    {
        MqttPacketType type{MqttPacketType::Connect};
        uint8_t flags{0};
        std::vector<uint8_t> body;
    };

    /* @brief A Publish packet's contents. `payload` points into the packet. */
    struct MqttPublish
    {
        std::string topic;
        uint8_t qos{0};
        uint16_t packetId{0};
        uint8_t* payload{nullptr};
        size_t length{0};
    };

    /// CONNECT with a clean session. `username` and `password` may be null.
    std::vector<uint8_t> encodeConnect(const char* clientId, const char* username,
                                       const char* password, uint16_t keepAliveSeconds);
    /// PUBLISH at QoS 0.
    std::vector<uint8_t> encodePublish(const char* topic, const char* payload, bool retained);
    std::vector<uint8_t> encodeSubscribe(uint16_t packetId, const char* topic, uint8_t qos);
    std::vector<uint8_t> encodePubAck(uint16_t packetId);
    std::vector<uint8_t> encodePingReq();
    std::vector<uint8_t> encodeDisconnect();

    /// False on a Publish too short to be one.
    bool parsePublish(MqttPacket& packet, MqttPublish& outPublish);

    /* @brief Cuts a byte stream into packets, however the bytes arrive.
    *
    *     reader.feed(bytes, count);
    *     MqttPacket packet;
    *     while (reader.next(packet)) { }
    *     if (reader.failed()) { }    // the stream is not MQTT: drop the connection
    */
    class MqttPacketReader
    {
    public:
        void feed(const uint8_t* data, size_t length);
        bool next(MqttPacket& outPacket);
        bool failed() const { return bFailed; }
        void reset();

    private:
        std::vector<uint8_t> buffer;
        bool bFailed{false};
    };
}
