// Copyright 2025 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "mqtt_transport_serial.h"
#include "../ecore/logging.h"

using namespace emqtt;
using namespace ecore::log;

MqttTransportSerial::MqttTransportSerial()
    : bConnected(false)
    , storedClientId(nullptr)
{
}

bool MqttTransportSerial::init(const char* server, uint16_t port, const char* clientId,
                               const char* username, const char* password)
{
    storedClientId = clientId;
    dbgLog("Serial MQTT transport initialized", Verbosity::Display, Category::IO);
    return true;
}

bool MqttTransportSerial::connect()
{
    if (bConnected)
    {
        return true;
    }

    dbgLog("Connecting to MQTT broker via Serial", Verbosity::Display, Category::IO);

    sendSerialMqtt(2, "");
    bConnected = true;

    dbgLog("Serial MQTT transport ready", Verbosity::Display, Category::IO);
    return true;
}

void MqttTransportSerial::disconnect()
{
    sendSerialMqtt(3, "");
    bConnected = false;
    dbgLog("Disconnected from Serial MQTT transport", Verbosity::Display, Category::IO);
}

bool MqttTransportSerial::isConnected()
{
    return bConnected;
}

bool MqttTransportSerial::publish(const char* topic, const char* payload, bool retained)
{
    if (!bConnected)
    {
        dbgLog("Cannot publish - not connected", Verbosity::Warning, Category::IO);
        return false;
    }

    sendSerialMqtt(1, topic, payload, 0, retained ? 1 : 0);
    return true;
}

bool MqttTransportSerial::subscribe(const char* topic, uint8_t qos)
{
    if (!bConnected)
    {
        dbgLog("Cannot subscribe - not connected", Verbosity::Warning, Category::IO);
        return false;
    }

    sendSerialMqtt(0, topic, "", qos, 0);
    return true;
}

void MqttTransportSerial::loop()
{
    processIncomingSerial();
}

void MqttTransportSerial::setCallback(MqttMessageCallback callback)
{
    userCallback = callback;
}

void MqttTransportSerial::setKeepAlive(uint16_t keepAliveSeconds)
{
}

void MqttTransportSerial::sendSerialMqtt(int cmd, const char* topic, const char* payload, int qos, int retained)
{
    Serial.print("[");
    Serial.print(cmd);

    if (topic && topic[0] != '\0')
    {
        Serial.print(",\"");
        Serial.print(topic);
        Serial.print("\"");

        if (payload && payload[0] != '\0')
        {
            Serial.print(",\"");
            Serial.print(payload);
            Serial.print("\"");

            if (qos > 0 || retained > 0)
            {
                Serial.print(",");
                Serial.print(qos);
                Serial.print(",");
                Serial.print(retained);
            }
        }
    }

    Serial.println("]");
}

void MqttTransportSerial::processIncomingSerial()
{
    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n' || c == '\r')
        {
            if (receiveBuffer.length() > 0)
            {
                char topic[128] = {0};
                char payload[256] = {0};

                if (parseJsonArray(receiveBuffer.c_str(), topic, payload, sizeof(payload)))
                {
                    if (userCallback && topic[0] != '\0')
                    {
                        userCallback(topic, (uint8_t*)payload, strlen(payload));
                    }
                }

                receiveBuffer = "";
            }
        }
        else
        {
            receiveBuffer += c;

            if (receiveBuffer.length() > 512)
            {
                receiveBuffer = "";
            }
        }
    }
}

bool MqttTransportSerial::parseJsonArray(const char* json, char* outTopic, char* outPayload, size_t maxLen)
{
    if (!json || json[0] != '[')
    {
        return false;
    }

    const char* p = json + 1;
    int fieldIndex = 0;
    bool inQuotes = false;
    char currentField[256] = {0};
    int fieldPos = 0;

    while (*p != '\0' && *p != ']')
    {
        if (*p == '"')
        {
            inQuotes = !inQuotes;
            p++;
            continue;
        }

        if (*p == ',' && !inQuotes)
        {
            currentField[fieldPos] = '\0';

            if (fieldIndex == 1 && outTopic)
            {
                strncpy(outTopic, currentField, 127);
                outTopic[127] = '\0';
            }
            else if (fieldIndex == 2 && outPayload)
            {
                strncpy(outPayload, currentField, maxLen - 1);
                outPayload[maxLen - 1] = '\0';
            }

            fieldIndex++;
            fieldPos = 0;
            p++;
            continue;
        }

        if (fieldPos < sizeof(currentField) - 1)
        {
            currentField[fieldPos++] = *p;
        }
        p++;
    }

    currentField[fieldPos] = '\0';
    if (fieldIndex == 2 && outPayload)
    {
        strncpy(outPayload, currentField, maxLen - 1);
        outPayload[maxLen - 1] = '\0';
    }

    return (fieldIndex >= 1);
}
