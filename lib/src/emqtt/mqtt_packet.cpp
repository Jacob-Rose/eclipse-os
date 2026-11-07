// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "mqtt_packet.h"

#include <cstring>

using namespace emqtt;

namespace
{
    constexpr size_t kMaxPacketBytes = 256 * 1024;

    void appendUint16(std::vector<uint8_t>& out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>(value >> 8));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void appendString(std::vector<uint8_t>& out, const char* text)
    {
        const size_t length = std::strlen(text);
        appendUint16(out, static_cast<uint16_t>(length));
        out.insert(out.end(), text, text + length);
    }

    std::vector<uint8_t> frame(uint8_t firstByte, const std::vector<uint8_t>& body)
    {
        std::vector<uint8_t> out;
        out.reserve(body.size() + 5);
        out.push_back(firstByte);

        size_t remaining = body.size();
        do
        {
            uint8_t digit = remaining % 128;
            remaining /= 128;
            if (remaining > 0)
            {
                digit |= 0x80;
            }
            out.push_back(digit);
        } while (remaining > 0);

        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    uint8_t firstByte(MqttPacketType type, uint8_t flags = 0)
    {
        return static_cast<uint8_t>((static_cast<uint8_t>(type) << 4) | flags);
    }
}

std::vector<uint8_t> emqtt::encodeConnect(const char* clientId, const char* username,
                                          const char* password, uint16_t keepAliveSeconds)
{
    std::vector<uint8_t> body;
    appendString(body, "MQTT");
    body.push_back(4);  // protocol level: 3.1.1

    uint8_t flags = 0x02;  // clean session
    if (username)
    {
        flags |= 0x80;
    }
    if (username && password)
    {
        flags |= 0x40;
    }
    body.push_back(flags);
    appendUint16(body, keepAliveSeconds);

    appendString(body, clientId);
    if (username)
    {
        appendString(body, username);
    }
    if (username && password)
    {
        appendString(body, password);
    }
    return frame(firstByte(MqttPacketType::Connect), body);
}

std::vector<uint8_t> emqtt::encodePublish(const char* topic, const char* payload, bool retained)
{
    std::vector<uint8_t> body;
    appendString(body, topic);
    body.insert(body.end(), payload, payload + std::strlen(payload));
    return frame(firstByte(MqttPacketType::Publish, retained ? 0x01 : 0x00), body);
}

std::vector<uint8_t> emqtt::encodeSubscribe(uint16_t packetId, const char* topic, uint8_t qos)
{
    std::vector<uint8_t> body;
    appendUint16(body, packetId);
    appendString(body, topic);
    body.push_back(qos);
    return frame(firstByte(MqttPacketType::Subscribe, 0x02), body);
}

std::vector<uint8_t> emqtt::encodePubAck(uint16_t packetId)
{
    std::vector<uint8_t> body;
    appendUint16(body, packetId);
    return frame(firstByte(MqttPacketType::PubAck), body);
}

std::vector<uint8_t> emqtt::encodePingReq()
{
    return frame(firstByte(MqttPacketType::PingReq), {});
}

std::vector<uint8_t> emqtt::encodeDisconnect()
{
    return frame(firstByte(MqttPacketType::Disconnect), {});
}

bool emqtt::parsePublish(MqttPacket& packet, MqttPublish& outPublish)
{
    std::vector<uint8_t>& body = packet.body;
    if (body.size() < 2)
    {
        return false;
    }

    const size_t topicLength = (static_cast<size_t>(body[0]) << 8) | body[1];
    size_t at = 2 + topicLength;
    if (at > body.size())
    {
        return false;
    }

    outPublish.topic.assign(reinterpret_cast<const char*>(body.data()) + 2, topicLength);
    outPublish.qos = (packet.flags >> 1) & 0x03;
    outPublish.packetId = 0;

    if (outPublish.qos > 0)
    {
        if (at + 2 > body.size())
        {
            return false;
        }
        outPublish.packetId = static_cast<uint16_t>((body[at] << 8) | body[at + 1]);
        at += 2;
    }

    outPublish.payload = body.data() + at;
    outPublish.length = body.size() - at;
    return true;
}

void MqttPacketReader::feed(const uint8_t* data, size_t length)
{
    buffer.insert(buffer.end(), data, data + length);
}

bool MqttPacketReader::next(MqttPacket& outPacket)
{
    if (bFailed || buffer.size() < 2)
    {
        return false;
    }

    size_t remaining = 0;
    size_t multiplier = 1;
    size_t at = 1;
    while (true)
    {
        if (at >= buffer.size())
        {
            return false;
        }
        if (at > 4)
        {
            bFailed = true;
            return false;
        }
        const uint8_t digit = buffer[at++];
        remaining += (digit & 0x7F) * multiplier;
        multiplier *= 128;
        if ((digit & 0x80) == 0)
        {
            break;
        }
    }

    if (remaining > kMaxPacketBytes)
    {
        bFailed = true;
        return false;
    }
    if (buffer.size() < at + remaining)
    {
        return false;
    }

    outPacket.type = static_cast<MqttPacketType>(buffer[0] >> 4);
    outPacket.flags = buffer[0] & 0x0F;
    outPacket.body.assign(buffer.begin() + static_cast<std::ptrdiff_t>(at),
                          buffer.begin() + static_cast<std::ptrdiff_t>(at + remaining));
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(at + remaining));
    return true;
}

void MqttPacketReader::reset()
{
    buffer.clear();
    bFailed = false;
}
