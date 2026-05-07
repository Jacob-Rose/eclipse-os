// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "mqtt_client.h"

using namespace emqtt;

MqttClient::MqttClient(std::unique_ptr<IMqttTransport> inTransport)
    : transport(std::move(inTransport))
    , config("", "")
    , status(ConnectionStatus::DISCONNECTED)
    , bAutoReconnect(true)
    , bRequireWiFi(true)
    , reconnectBaseInterval(5.0f)
    , reconnectMaxInterval(120.0f)
    , currentReconnectInterval(5.0f)
    , timeSinceLastReconnect(0.0f)
    , reconnectAttempts(0)
{
    transport->setCallback([this](const char* topic, uint8_t* payload, unsigned int length) {
        if (userCallback)
        {
            userCallback(topic, payload, length);
        }
    });
}

void MqttClient::init(const MqttConfig& inConfig)
{
    config = inConfig;
    transport->init(config.server, config.port, config.clientId, config.username, config.password);
    transport->setKeepAlive(config.keepAlive);

    log::dbgLog("MQTT client initialized", log::Verbosity::Display, log::Category::IO);
}

void MqttClient::setTopics(const TopicConfig& inTopics)
{
    topics = inTopics;
}

void MqttClient::setCallback(MqttCallback callback)
{
    userCallback = callback;
}

bool MqttClient::connect()
{
    if (isConnected())
    {
        return true;
    }

    status = ConnectionStatus::CONNECTING;
    log::dbgLog("Connecting to MQTT broker", log::Verbosity::Display, log::Category::IO);

    bool connected = transport->connect();

    if (connected)
    {
        status = ConnectionStatus::CONNECTED;
        reconnectAttempts = 0;
        currentReconnectInterval = reconnectBaseInterval;

        log::dbgLog("Connected to MQTT broker", log::Verbosity::Display, log::Category::IO);

        subscribeToTopics();

        if (topics.availabilityTopic)
        {
            publish(topics.availabilityTopic, "online", true);
        }

        return true;
    }
    else
    {
        status = ConnectionStatus::FAILED;
        log::dbgLog("Failed to connect to MQTT broker", log::Verbosity::Error, log::Category::IO);
        return false;
    }
}

void MqttClient::disconnect()
{
    if (topics.availabilityTopic)
    {
        publish(topics.availabilityTopic, "offline", true);
    }

    transport->disconnect();
    status = ConnectionStatus::DISCONNECTED;
    reconnectAttempts = 0;
    currentReconnectInterval = reconnectBaseInterval;
    log::dbgLog("Disconnected from MQTT broker", log::Verbosity::Display, log::Category::IO);
}

bool MqttClient::publish(const char* topic, const char* payload, bool retained)
{
    if (!isConnected())
    {
        log::dbgLog("Cannot publish - not connected", log::Verbosity::Warning, log::Category::IO);
        return false;
    }

    // Log topic and payload size for debugging
    char debugMsg[128];
    int payloadLen = strlen(payload);
    snprintf(debugMsg, sizeof(debugMsg), "Publishing to %s (%d bytes)", topic, payloadLen);
    log::dbgLog(debugMsg, log::Verbosity::Verbose, log::Category::IO);

    bool success = transport->publish(topic, payload, retained);

    if (success)
    {
        log::dbgLog("MQTT message published", log::Verbosity::Verbose, log::Category::IO);
    }
    else
    {
        snprintf(debugMsg, sizeof(debugMsg), "Failed to publish to %s (%d bytes)", topic, payloadLen);
        log::dbgLog(debugMsg, log::Verbosity::Error, log::Category::IO);
    }

    return success;
}

bool MqttClient::subscribe(const char* topic, uint8_t qos)
{
    if (!isConnected())
    {
        log::dbgLog("Cannot subscribe - not connected", log::Verbosity::Warning, log::Category::IO);
        return false;
    }

    bool success = transport->subscribe(topic, qos);

    if (success)
    {
        log::dbgLog("Subscribed to MQTT topic", log::Verbosity::Display, log::Category::IO);
    }
    else
    {
        log::dbgLog("Failed to subscribe to MQTT topic", log::Verbosity::Error, log::Category::IO);
    }

    return success;
}

bool MqttClient::isConnected()
{
    return transport->isConnected();
}

ConnectionStatus MqttClient::getStatus() const
{
    return status;
}

void MqttClient::tick(float deltaTime)
{
    if (isConnected())
    {
        transport->loop();
        status = ConnectionStatus::CONNECTED;

        if (reconnectAttempts > 0)
        {
            reconnectAttempts = 0;
            currentReconnectInterval = reconnectBaseInterval;
        }
    }
    else
    {
        status = ConnectionStatus::DISCONNECTED;

        if (bAutoReconnect)
        {
            // Don't attempt MQTT reconnect if WiFi isn't available
            if (bRequireWiFi && wifiConnectedCheck && !wifiConnectedCheck())
            {
                // Reset timer so we try promptly once WiFi is back
                timeSinceLastReconnect = 0.0f;
                return;
            }

            timeSinceLastReconnect += deltaTime;

            if (timeSinceLastReconnect >= currentReconnectInterval)
            {
                timeSinceLastReconnect = 0.0f;
                attemptReconnect();
            }
        }
    }
}

float MqttClient::getNextReconnectInterval() const
{
    // Exponential backoff: base * 2^attempts, capped at max
    float interval = reconnectBaseInterval;
    for (int i = 0; i < reconnectAttempts && interval < reconnectMaxInterval; i++)
    {
        interval *= 2.0f;
    }
    if (interval > reconnectMaxInterval)
    {
        interval = reconnectMaxInterval;
    }
    return interval;
}

void MqttClient::attemptReconnect()
{
    reconnectAttempts++;

    char debugMsg[96];
    snprintf(debugMsg, sizeof(debugMsg), "MQTT reconnect attempt %d (next in %.0fs)",
             reconnectAttempts, getNextReconnectInterval());
    log::dbgLog(debugMsg, log::Verbosity::Display, log::Category::IO);

    bool success = connect();

    if (!success)
    {
        currentReconnectInterval = getNextReconnectInterval();
    }
}

void MqttClient::subscribeToTopics()
{
    if (topics.commandTopic)
    {
        subscribe(topics.commandTopic, topics.qos);
    }

    if (topics.brightnessTopic)
    {
        subscribe(topics.brightnessTopic, topics.qos);
    }

    if (topics.effectTopic)
    {
        subscribe(topics.effectTopic, topics.qos);
    }
}
