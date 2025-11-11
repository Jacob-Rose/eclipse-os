// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>
#include <vector>
#include <stdint.h>

#include "mqtt_client.h"
#include "mqtt_config.h"
#include "../ecore/logging.h"

using namespace ecore;

namespace emqtt
{
    class HomeAssistantDiscovery
    {
    public:
        HomeAssistantDiscovery(MqttClient& client);

        void init(const HomeAssistantConfig& config);

        bool publishLightDiscovery(
            const char* uniqueId,
            const char* name,
            const std::vector<std::string>& effectList = {}
        );

        bool publishSensorDiscovery(
            const char* uniqueId,
            const char* name,
            const char* deviceClass,
            const char* unit
        );

        bool publishSwitchDiscovery(
            const char* uniqueId,
            const char* name
        );

        bool publishSelectDiscovery(
            const char* uniqueId,
            const char* name,
            const std::vector<std::string>& options
        );

        bool removeDiscovery(const char* component, const char* uniqueId);

        std::string buildDeviceInfo() const;
        std::string buildOriginInfo() const;
        std::string getDiscoveryTopic(const char* component, const char* uniqueId) const;

    private:
        std::string buildDiscoveryPayload(const std::string& config) const;
        static std::string escapeJson(const std::string& str);

        MqttClient& mqttClient;
        HomeAssistantConfig haConfig;
        bool bInitialized;
    };

    class EffectListBuilder
    {
    public:
        EffectListBuilder() = default;

        EffectListBuilder& add(const std::string& effectName);
        const std::vector<std::string>& build() const { return effects; }

    private:
        std::vector<std::string> effects;
    };
}
