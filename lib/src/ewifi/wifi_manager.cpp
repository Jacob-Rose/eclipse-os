// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "wifi_manager.h"

#if USE_ARDUINO

using namespace ewifi;
using namespace ecore::log;

namespace
{
    constexpr int kSocketTimeoutMs = 1500;
    constexpr uint32_t kMaxRetryWaitMs = 300000;
}

WiFiManager::WiFiManager()
    : ssid(nullptr)
    , password(nullptr)
    , status(WiFiStatus::DISCONNECTED)
    , bAutoReconnect(true)
{
    ipAddressBuffer[0] = '\0';
    retry.setBase(30000);
    retry.setCap(kMaxRetryWaitMs);
}

void WiFiManager::init(const char* inSsid, const char* inPassword)
{
    ssid = inSsid;
    password = inPassword;
    
    dbgLog("WiFi manager initialized", Verbosity::Display, Category::IO);
}

void WiFiManager::begin()
{
    // A publish waits on nothing but its own buffer, and a broker that does
    // not answer costs this long rather than the client's default 5s.
    WiFiClient::setDefaultSync(false);
    wifiClient.setTimeout(kSocketTimeoutMs);

    startJoin(millis());
}

void WiFiManager::startJoin(uint32_t now)
{
    status = WiFiStatus::CONNECTING;
    joinStartedAt = now;
    dbgLog("Connecting to WiFi...", Verbosity::Display, Category::IO);

    WiFi.mode(WIFI_STA);

    // The radio's power save naps between beacons and drops what arrives
    // while it is asleep; on a Pico W that is an MQTT command taking fifteen
    // seconds of TCP retransmits to land, one time in three. A sculpture on
    // mains has no battery to save. Before begin(), which is where the
    // driver reads it.
    WiFi.noLowPowerMode();

    // Not begin(), which waits up to twice WiFi's 15s timeout for the join.
    WiFi.beginNoBlock(ssid, password);
}

void WiFiManager::onJoined()
{
    status = WiFiStatus::CONNECTED;
    retry.succeeded();

    IPAddress ip = WiFi.localIP();
    snprintf(ipAddressBuffer, sizeof(ipAddressBuffer), "%d.%d.%d.%d",
             ip[0], ip[1], ip[2], ip[3]);

    dbgLog("WiFi connected", Verbosity::Display, Category::IO);
}

void WiFiManager::disconnect()
{
    WiFi.disconnect();
    status = WiFiStatus::DISCONNECTED;
    ipAddressBuffer[0] = '\0';
    
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
    (void)deltaTime;

    if (isConnected())
    {
        if (status != WiFiStatus::CONNECTED)
        {
            onJoined();
        }
        return;
    }

    const uint32_t now = millis();

    if (status == WiFiStatus::CONNECTING)
    {
        if (now - joinStartedAt < connectTimeoutMs)
        {
            return;
        }
        status = WiFiStatus::FAILED;
        retry.failed(now);
        dbgLog("WiFi connection failed", Verbosity::Error, Category::IO);
    }
    else if (status == WiFiStatus::CONNECTED)
    {
        status = WiFiStatus::DISCONNECTED;
        ipAddressBuffer[0] = '\0';
        dbgLog("WiFi lost", Verbosity::Warning, Category::IO);
    }

    if (bAutoReconnect && retry.due(now))
    {
        startJoin(now);
    }
}

#endif
