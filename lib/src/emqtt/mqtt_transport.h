// Copyright 2025 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <cstdint>

namespace emqtt
{
    using MqttMessageCallback = std::function<void(const char* topic, uint8_t* payload, unsigned int length)>;

    class IMqttTransport
    {
    public:
        virtual ~IMqttTransport() = default;

        virtual bool init(const char* server, uint16_t port, const char* clientId,
                         const char* username = nullptr, const char* password = nullptr) = 0;

        virtual bool connect() = 0;
        virtual void disconnect() = 0;
        virtual bool isConnected() = 0;

        virtual bool publish(const char* topic, const char* payload, bool retained = false) = 0;
        virtual bool subscribe(const char* topic, uint8_t qos = 0) = 0;

        virtual void loop() = 0;

        virtual void setCallback(MqttMessageCallback callback) = 0;
        virtual void setKeepAlive(uint16_t keepAliveSeconds) = 0;
    };
}
