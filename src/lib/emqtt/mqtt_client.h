// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>
#include <functional>

#include "../ecore/tickable.h"
#include "../ecore/logging.h"
#include "mqtt_config.h"
#include "mqtt_transport.h"

using namespace ecore;

namespace emqtt
{
    using MqttCallback = std::function<void(const char* topic, uint8_t* payload, unsigned int length)>;

    class MqttClient : public Tickable
    {
    public:
        explicit MqttClient(std::unique_ptr<IMqttTransport> transport);

        void init(const MqttConfig& config);
        void setTopics(const TopicConfig& topics);
        void setCallback(MqttCallback callback);

        bool connect();
        void disconnect();
        bool publish(const char* topic, const char* payload, bool retained = false);
        bool subscribe(const char* topic, uint8_t qos = 0);

        bool isConnected();
        ConnectionStatus getStatus() const;

        virtual void tick(float deltaTime) override;

        void setReconnectInterval(float seconds) { reconnectInterval = seconds; }
        void setAutoReconnect(bool enabled) { bAutoReconnect = enabled; }

    private:
        void attemptReconnect();
        void subscribeToTopics();

        std::unique_ptr<IMqttTransport> transport;
        MqttConfig config;
        TopicConfig topics;

        MqttCallback userCallback;

        ConnectionStatus status;
        bool bAutoReconnect;
        float reconnectInterval;
        float timeSinceLastReconnect;
    };
}
