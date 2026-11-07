// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#if USE_ARDUINO

#include <WiFi.h>
#include <memory>

#include <ecore/backoff.h>
#include <ecore/tickable.h>
#include <ecore/logging.h>

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

    /* @brief Keeps the board on the network without ever waiting for it.
    *
    * begin() starts joining and returns; tick() watches the join, gives up on
    * it after the connect timeout, and retries with a growing wait.
    *
    *     wifi.init(ssid, password);
    *     wifi.begin();
    *     wifi.tick(dt);    // every loop
    */
    class WiFiManager : public Tickable
    {
    public:
        WiFiManager();
        
        void init(const char* ssid, const char* password);

        /// Starts joining the network and returns at once.
        void begin();
        void disconnect();
        
        bool isConnected() const;
        WiFiStatus getStatus() const;
        const char* getLocalIP() const;
        
        WiFiClient& getClient();
        
        virtual void tick(float deltaTime) override;
        
        void setAutoReconnect(bool enabled) { bAutoReconnect = enabled; }

        /// The first wait after a failed join; each further failure doubles it.
        void setReconnectInterval(float seconds) { retry.setBase(static_cast<uint32_t>(seconds * 1000.0f)); }

        /// How long one join gets before it counts as failed.
        void setConnectTimeout(float seconds) { connectTimeoutMs = static_cast<uint32_t>(seconds * 1000.0f); }

    private:
        void startJoin(uint32_t now);
        void onJoined();

        const char* ssid;
        const char* password;
        
        WiFiClient wifiClient;
        WiFiStatus status;
        
        bool bAutoReconnect;
        Backoff retry;
        uint32_t connectTimeoutMs{10000};
        uint32_t joinStartedAt{0};

        char ipAddressBuffer[16];
    };
}

#endif
