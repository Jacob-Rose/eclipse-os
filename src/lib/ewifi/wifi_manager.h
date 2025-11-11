// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <WiFi.h>
#include <memory>

#include "../ecore/tickable.h"
#include "../ecore/logging.h"

using namespace ecore;

namespace ewifi
{
    enum class WiFiStatus
    {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED
    };

    class WiFiManager : public Tickable
    {
    public:
        WiFiManager();
        
        void init(const char* ssid, const char* password);
        bool connect(int maxAttempts = 20);
        void disconnect();
        
        bool isConnected() const;
        WiFiStatus getStatus() const;
        const char* getLocalIP() const;
        
        WiFiClient& getClient();
        
        virtual void tick(float deltaTime) override;
        
        void setAutoReconnect(bool enabled) { bAutoReconnect = enabled; }
        void setReconnectInterval(float seconds) { reconnectInterval = seconds; }

    private:
        void attemptReconnect();
        
        const char* ssid;
        const char* password;
        
        WiFiClient wifiClient;
        WiFiStatus status;
        
        bool bAutoReconnect;
        float reconnectInterval;
        float timeSinceLastReconnect;
        
        char ipAddressBuffer[16];
    };
}
