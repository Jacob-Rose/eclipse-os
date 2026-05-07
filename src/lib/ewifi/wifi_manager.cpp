// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "wifi_manager.h"

using namespace ewifi;
using namespace ecore::log;

WiFiManager::WiFiManager()
    : ssid(nullptr)
    , password(nullptr)
    , status(WiFiStatus::DISCONNECTED)
    , bAutoReconnect(true)
    , reconnectBaseInterval(30.0f)
    , reconnectMaxInterval(300.0f)
    , currentReconnectInterval(30.0f)
    , timeSinceLastReconnect(0.0f)
    , reconnectAttempts(0)
{
    ipAddressBuffer[0] = '\0';
}

void WiFiManager::init(const char* inSsid, const char* inPassword)
{
    ssid = inSsid;
    password = inPassword;

    dbgLog("WiFi manager initialized", Verbosity::Display, Category::IO);
}

bool WiFiManager::connect(int maxAttempts)
{
    if (isConnected())
    {
        return true;
    }

    status = WiFiStatus::CONNECTING;
    dbgLog("Connecting to WiFi...", Verbosity::Display, Category::IO);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts)
    {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        status = WiFiStatus::CONNECTED;
        reconnectAttempts = 0;
        currentReconnectInterval = reconnectBaseInterval;

        IPAddress ip = WiFi.localIP();
        snprintf(ipAddressBuffer, sizeof(ipAddressBuffer), "%d.%d.%d.%d",
                 ip[0], ip[1], ip[2], ip[3]);

        dbgLog("WiFi connected", Verbosity::Display, Category::IO);

        return true;
    }
    else
    {
        status = WiFiStatus::FAILED;
        dbgLog("WiFi connection failed", Verbosity::Error, Category::IO);
        return false;
    }
}

void WiFiManager::disconnect()
{
    WiFi.disconnect();
    status = WiFiStatus::DISCONNECTED;
    ipAddressBuffer[0] = '\0';
    reconnectAttempts = 0;
    currentReconnectInterval = reconnectBaseInterval;

    dbgLog("WiFi disconnected", Verbosity::Display, Category::IO);
}

bool WiFiManager::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

WiFiStatus WiFiManager::getStatus() const
{
    return status;
}

const char* WiFiManager::getLocalIP() const
{
    return ipAddressBuffer;
}

WiFiClient& WiFiManager::getClient()
{
    return wifiClient;
}

void WiFiManager::tick(float deltaTime)
{
    if (isConnected())
    {
        status = WiFiStatus::CONNECTED;

        if (reconnectAttempts > 0)
        {
            reconnectAttempts = 0;
            currentReconnectInterval = reconnectBaseInterval;
        }
    }
    else
    {
        status = WiFiStatus::DISCONNECTED;

        if (bAutoReconnect)
        {
            timeSinceLastReconnect += deltaTime;

            if (timeSinceLastReconnect >= currentReconnectInterval)
            {
                timeSinceLastReconnect = 0.0f;
                attemptReconnect();
            }
        }
    }
}

float WiFiManager::getNextReconnectInterval() const
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

void WiFiManager::attemptReconnect()
{
    reconnectAttempts++;

    char debugMsg[96];
    snprintf(debugMsg, sizeof(debugMsg), "WiFi reconnect attempt %d (next in %.0fs)",
             reconnectAttempts, getNextReconnectInterval());
    dbgLog(debugMsg, Verbosity::Display, Category::IO);

    // Use fewer blocking attempts during reconnect to avoid stalling the main loop
    bool success = connect(5);

    if (!success)
    {
        currentReconnectInterval = getNextReconnectInterval();
    }
}
