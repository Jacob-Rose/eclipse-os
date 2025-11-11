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
    , reconnectInterval(30.0f)
    , timeSinceLastReconnect(0.0f)
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
    }
    else
    {
        status = WiFiStatus::DISCONNECTED;
        
        if (bAutoReconnect)
        {
            timeSinceLastReconnect += deltaTime;
            
            if (timeSinceLastReconnect >= reconnectInterval)
            {
                timeSinceLastReconnect = 0.0f;
                attemptReconnect();
            }
        }
    }
}

void WiFiManager::attemptReconnect()
{
    dbgLog("Attempting to reconnect to WiFi", Verbosity::Display, Category::IO);
    connect();
}
