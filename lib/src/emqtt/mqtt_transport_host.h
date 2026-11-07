// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#include "mqtt_transport.h"

#if !USE_ARDUINO

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "mqtt_packet.h"

namespace emqtt
{
    /* @brief MQTT over a TCP socket on a desk: the host's answer to
    * MqttTransportWiFi, behind the same MqttClient.
    *
    *     MqttClient client(std::make_unique<MqttTransportHost>());
    *     client.init(MqttConfig("192.168.1.100", "my-desk"));
    *     client.tick(dt);    // connects, reconnects, and pumps messages
    *
    * loop() never waits. connect() and a stalled send wait no longer than
    * their timeouts. Sends at QoS 0.
    */
    class MqttTransportHost : public IMqttTransport
    {
    public:
        MqttTransportHost() = default;
        ~MqttTransportHost() override;

        MqttTransportHost(const MqttTransportHost&) = delete;
        MqttTransportHost& operator=(const MqttTransportHost&) = delete;

        bool init(const char* server, uint16_t port, const char* clientId,
                  const char* username = nullptr, const char* password = nullptr) override;

        bool connect() override;
        void disconnect() override;
        bool isConnected() override;

        bool publish(const char* topic, const char* payload, bool retained = false) override;
        bool subscribe(const char* topic, uint8_t qos = 0) override;

        void loop() override;

        void setCallback(MqttMessageCallback callback) override { userCallback = std::move(callback); }
        void setKeepAlive(uint16_t keepAliveSeconds) override { keepAlive = keepAliveSeconds; }

        /// The longest connect() spends on the TCP connect, and again on the
        /// broker's answer, and the longest a stalled send waits. 1500 by default.
        void setTimeout(int milliseconds) { timeoutMs = milliseconds; }

    private:
        bool openSocket();
        bool awaitConnAck();
        bool sendBytes(const std::vector<uint8_t>& bytes);
        void handlePacket(MqttPacket& packet);
        void closeSocket();

        std::string server;
        uint16_t port{1883};
        std::string clientId;
        std::string username;
        std::string password;
        bool bHasUsername{false};
        bool bHasPassword{false};

        intptr_t handle{-1};
        bool bSessionUp{false};
        MqttPacketReader reader;
        MqttMessageCallback userCallback;

        uint16_t keepAlive{15};
        int timeoutMs{1500};
        uint16_t nextPacketId{1};
        uint32_t lastSentAt{0};
        uint32_t pingSentAt{0};
        bool bPingOutstanding{false};
    };
}

#endif
