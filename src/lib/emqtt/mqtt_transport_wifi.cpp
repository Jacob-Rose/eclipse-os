// Copyright 2025 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "mqtt_transport_wifi.h"
#include "../ecore/logging.h"

using namespace emqtt;
using namespace ecore::log;

MqttTransportWiFi* MqttTransportWiFi::instance = nullptr;

MqttTransportWiFi::MqttTransportWiFi(Client& client)
    : mqttClient(client)
    , storedServer(nullptr)
    , storedPort(1883)
    , storedClientId(nullptr)
    , storedUsername(nullptr)
    , storedPassword(nullptr)
{
    instance = this;
    mqttClient.setCallback(MqttTransportWiFi::internalCallback);

    // Set buffer size for larger messages (HA discovery payloads can be 500-1500 bytes)
    mqttClient.setBufferSize(2048);

    dbgLog("MQTT buffer size set to 2048 bytes", Verbosity::Display, Category::IO);
}

bool MqttTransportWiFi::init(const char* server, uint16_t port, const char* clientId,
                             const char* username, const char* password)
{
    storedServer = server;
    storedPort = port;
    storedClientId = clientId;
    storedUsername = username;
    storedPassword = password;

    mqttClient.setServer(server, port);

    dbgLog("WiFi MQTT transport initialized", Verbosity::Display, Category::IO);
    return true;
}

bool MqttTransportWiFi::connect()
{
    if (isConnected())
    {
        return true;
    }

    char connectMsg[128];
    snprintf(connectMsg, sizeof(connectMsg), "Connecting to MQTT broker via WiFi: %s:%d", storedServer, storedPort);
    dbgLog(connectMsg, Verbosity::Display, Category::IO);

    bool connected = false;

    if (storedUsername && storedPassword)
    {
        connected = mqttClient.connect(storedClientId, storedUsername, storedPassword);
    }
    else
    {
        connected = mqttClient.connect(storedClientId);
    }

    if (connected)
    {
        dbgLog("Connected to MQTT broker", Verbosity::Display, Category::IO);
    }
    else
    {
        char errorMsg[64];
        int state = mqttClient.state();
        snprintf(errorMsg, sizeof(errorMsg), "Failed to connect to MQTT broker (state: %d)", state);
        dbgLog(errorMsg, Verbosity::Error, Category::IO);

        // Log human-readable error
        const char* stateMsg = "";
        switch(state) {
            case -4: stateMsg = "Connection timeout"; break;
            case -3: stateMsg = "Connection lost"; break;
            case -2: stateMsg = "Connect failed"; break;
            case -1: stateMsg = "Disconnected"; break;
            case 1: stateMsg = "Bad protocol"; break;
            case 2: stateMsg = "Bad client ID"; break;
            case 3: stateMsg = "Unavailable"; break;
            case 4: stateMsg = "Bad credentials"; break;
            case 5: stateMsg = "Unauthorized"; break;
            default: stateMsg = "Unknown error"; break;
        }
        snprintf(errorMsg, sizeof(errorMsg), "MQTT Error: %s", stateMsg);
        dbgLog(errorMsg, Verbosity::Error, Category::IO);
    }

    return connected;
}

void MqttTransportWiFi::disconnect()
{
    mqttClient.disconnect();
    dbgLog("Disconnected from MQTT broker", Verbosity::Display, Category::IO);
}

bool MqttTransportWiFi::isConnected()
{
    return mqttClient.connected();
}

bool MqttTransportWiFi::publish(const char* topic, const char* payload, bool retained)
{
    if (!isConnected())
    {
        dbgLog("Cannot publish - not connected", Verbosity::Warning, Category::IO);
        return false;
    }

    return mqttClient.publish(topic, payload, retained);
}

bool MqttTransportWiFi::subscribe(const char* topic, uint8_t qos)
{
    if (!isConnected())
    {
        dbgLog("Cannot subscribe - not connected", Verbosity::Warning, Category::IO);
        return false;
    }

    return mqttClient.subscribe(topic, qos);
}

void MqttTransportWiFi::loop()
{
    mqttClient.loop();
}

void MqttTransportWiFi::setCallback(MqttMessageCallback callback)
{
    userCallback = callback;
}

void MqttTransportWiFi::setKeepAlive(uint16_t keepAliveSeconds)
{
    mqttClient.setKeepAlive(keepAliveSeconds);
}

void MqttTransportWiFi::internalCallback(char* topic, uint8_t* payload, unsigned int length)
{
    if (instance && instance->userCallback)
    {
        instance->userCallback(topic, payload, length);
    }
}
