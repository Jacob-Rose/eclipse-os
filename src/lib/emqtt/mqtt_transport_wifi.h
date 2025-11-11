// Copyright 2025 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "mqtt_transport.h"
#include <PubSubClient.h>
#include <Client.h>

namespace emqtt
{
    class MqttTransportWiFi : public IMqttTransport
    {
    public:
        explicit MqttTransportWiFi(Client& client);

        bool init(const char* server, uint16_t port, const char* clientId,
                 const char* username = nullptr, const char* password = nullptr) override;

        bool connect() override;
        void disconnect() override;
        bool isConnected() override;

        bool publish(const char* topic, const char* payload, bool retained = false) override;
        bool subscribe(const char* topic, uint8_t qos = 0) override;

        void loop() override;

        void setCallback(MqttMessageCallback callback) override;
        void setKeepAlive(uint16_t keepAliveSeconds) override;

    private:
        static void internalCallback(char* topic, uint8_t* payload, unsigned int length);

        PubSubClient mqttClient;
        MqttMessageCallback userCallback;

        const char* storedServer;
        uint16_t storedPort;
        const char* storedClientId;
        const char* storedUsername;
        const char* storedPassword;

        static MqttTransportWiFi* instance;
    };
}
