// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdint.h>

namespace emqtt
{
    struct MqttConfig
    {
        const char* server;
        uint16_t port = 1883;
        const char* clientId;
        const char* username = nullptr;
        const char* password = nullptr;
        uint16_t keepAlive = 15;
        bool cleanSession = true;
        
        MqttConfig(const char* inServer, const char* inClientId)
            : server(inServer)
            , clientId(inClientId)
        {}
    };

    struct TopicConfig
    {
        const char* stateTopic = nullptr;
        const char* commandTopic = nullptr;
        const char* brightnessTopic = nullptr;
        const char* effectTopic = nullptr;
        const char* availabilityTopic = nullptr;
        
        bool useRetained = true;
        uint8_t qos = 0;
    };

    struct HomeAssistantConfig
    {
        bool enabled = false;
        const char* discoveryPrefix = "homeassistant";
        const char* deviceName = nullptr;
        const char* deviceId = nullptr;
        const char* manufacturer = "Eclipse OS";
        const char* model = "LED Controller";
        const char* swVersion = "1.0.0";
    };

    enum class ConnectionStatus
    {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED
    };
}
