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
        int getReconnectAttempts() const { return reconnectAttempts; }

        WiFiClient& getClient();

        virtual void tick(float deltaTime) override;

        void setAutoReconnect(bool enabled) { bAutoReconnect = enabled; }
        void setReconnectInterval(float seconds) { reconnectBaseInterval = seconds; }
        void setMaxReconnectInterval(float seconds) { reconnectMaxInterval = seconds; }

    private:
        void attemptReconnect();
        float getNextReconnectInterval() const;

        const char* ssid;
        const char* password;

        WiFiClient wifiClient;
        WiFiStatus status;

        bool bAutoReconnect;
        float reconnectBaseInterval;
        float reconnectMaxInterval;
        float currentReconnectInterval;
        float timeSinceLastReconnect;
        int reconnectAttempts;

        char ipAddressBuffer[16];
    };
}
