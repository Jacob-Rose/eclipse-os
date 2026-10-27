// Copyright 2025 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "mqtt_transport.h"
#include <Arduino.h>

namespace emqtt
{
    class MqttTransportSerial : public IMqttTransport
    {
    public:
        MqttTransportSerial();

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
        void sendSerialMqtt(int cmd, const char* topic, const char* payload = "", int qos = 0, int retained = 0);
        void processIncomingSerial();
        bool parseJsonArray(const char* json, char* outTopic, char* outPayload, size_t maxLen);

        MqttMessageCallback userCallback;
        bool bConnected;
        const char* storedClientId;

        String receiveBuffer;
    };
}
