// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "ha_discovery.h"
#include <sstream>
#include <algorithm>

using namespace emqtt;

HomeAssistantDiscovery::HomeAssistantDiscovery(MqttClient& client)
    : mqttClient(client)
    , bInitialized(false)
{
}

void HomeAssistantDiscovery::init(const HomeAssistantConfig& config)
{
    haConfig = config;
    bInitialized = true;
    
    log::dbgLog("Home Assistant discovery initialized", log::Verbosity::Display, log::Category::IO);
}

bool HomeAssistantDiscovery::publishLightDiscovery(
    const char* uniqueId,
    const char* name,
    const std::vector<std::string>& effectList)
{
    if (!bInitialized)
    {
        log::dbgLog("Home Assistant discovery not initialized", log::Verbosity::Error, log::Category::IO);
        return false;
    }

    std::string topic = getDiscoveryTopic("light", uniqueId);
    
    std::ostringstream config;
    config << "{";
    config << "\"name\":\"" << escapeJson(name) << "\",";
    config << "\"unique_id\":\"" << escapeJson(uniqueId) << "\",";
    config << "\"command_topic\":\"eclipse/" << escapeJson(uniqueId) << "/set\",";
    config << "\"state_topic\":\"eclipse/" << escapeJson(uniqueId) << "/state\",";
    config << "\"brightness_command_topic\":\"eclipse/" << escapeJson(uniqueId) << "/brightness/set\",";
    config << "\"brightness_state_topic\":\"eclipse/" << escapeJson(uniqueId) << "/brightness\",";
    config << "\"brightness_scale\":255,";
    config << "\"rgb_command_topic\":\"eclipse/" << escapeJson(uniqueId) << "/rgb/set\",";
    config << "\"rgb_state_topic\":\"eclipse/" << escapeJson(uniqueId) << "/rgb\",";
    
    if (!effectList.empty())
    {
        config << "\"effect_command_topic\":\"eclipse/" << escapeJson(uniqueId) << "/effect/set\",";
        config << "\"effect_state_topic\":\"eclipse/" << escapeJson(uniqueId) << "/effect\",";
        config << "\"effect_list\":[";
        for (size_t i = 0; i < effectList.size(); ++i)
        {
            config << "\"" << escapeJson(effectList[i]) << "\"";
            if (i < effectList.size() - 1)
                config << ",";
        }
        config << "],";
    }
    
    config << "\"availability_topic\":\"eclipse/" << escapeJson(uniqueId) << "/available\",";
    config << "\"device\":" << buildDeviceInfo() << ",";
    config << "\"origin\":" << buildOriginInfo();
    config << "}";

    std::string payload = config.str();

    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "Light discovery payload: %d bytes", (int)payload.length());
    log::dbgLog(logMsg, log::Verbosity::Display, log::Category::IO);

    bool success = mqttClient.publish(topic.c_str(), payload.c_str(), true);

    if (success)
    {
        log::dbgLog("Published light discovery", log::Verbosity::Display, log::Category::IO);
    }
    else
    {
        log::dbgLog("Failed to publish light discovery", log::Verbosity::Error, log::Category::IO);
    }

    return success;
}

bool HomeAssistantDiscovery::publishSensorDiscovery(
    const char* uniqueId,
    const char* name,
    const char* deviceClass,
    const char* unit)
{
    if (!bInitialized)
    {
        log::dbgLog("Home Assistant discovery not initialized", log::Verbosity::Error, log::Category::IO);
        return false;
    }

    std::string topic = getDiscoveryTopic("sensor", uniqueId);
    
    std::ostringstream config;
    config << "{";
    config << "\"name\":\"" << escapeJson(name) << "\",";
    config << "\"unique_id\":\"" << escapeJson(uniqueId) << "\",";
    config << "\"state_topic\":\"eclipse/" << escapeJson(uniqueId) << "/state\",";
    config << "\"device_class\":\"" << escapeJson(deviceClass) << "\",";
    config << "\"unit_of_measurement\":\"" << escapeJson(unit) << "\",";
    config << "\"availability_topic\":\"eclipse/" << escapeJson(uniqueId) << "/available\",";
    config << "\"device\":" << buildDeviceInfo() << ",";
    config << "\"origin\":" << buildOriginInfo();
    config << "}";

    std::string payload = config.str();
    return mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

bool HomeAssistantDiscovery::publishSwitchDiscovery(
    const char* uniqueId,
    const char* name)
{
    if (!bInitialized)
    {
        log::dbgLog("Home Assistant discovery not initialized", log::Verbosity::Error, log::Category::IO);
        return false;
    }

    std::string topic = getDiscoveryTopic("switch", uniqueId);

    std::ostringstream config;
    config << "{";
    config << "\"name\":\"" << escapeJson(name) << "\",";
    config << "\"unique_id\":\"" << escapeJson(uniqueId) << "\",";
    config << "\"command_topic\":\"eclipse/" << escapeJson(uniqueId) << "/set\",";
    config << "\"state_topic\":\"eclipse/" << escapeJson(uniqueId) << "/state\",";
    config << "\"availability_topic\":\"eclipse/" << escapeJson(uniqueId) << "/available\",";
    config << "\"device\":" << buildDeviceInfo() << ",";
    config << "\"origin\":" << buildOriginInfo();
    config << "}";

    std::string payload = config.str();
    return mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

bool HomeAssistantDiscovery::publishSelectDiscovery(
    const char* uniqueId,
    const char* name,
    const std::vector<std::string>& options)
{
    if (!bInitialized)
    {
        log::dbgLog("Home Assistant discovery not initialized", log::Verbosity::Error, log::Category::IO);
        return false;
    }

    if (options.empty())
    {
        log::dbgLog("Select entity requires at least one option", log::Verbosity::Error, log::Category::IO);
        return false;
    }

    std::string topic = getDiscoveryTopic("select", uniqueId);

    std::ostringstream config;
    config << "{";
    config << "\"name\":\"" << escapeJson(name) << "\",";
    config << "\"unique_id\":\"" << escapeJson(uniqueId) << "\",";
    config << "\"command_topic\":\"eclipse/" << escapeJson(uniqueId) << "/set\",";
    config << "\"state_topic\":\"eclipse/" << escapeJson(uniqueId) << "/state\",";
    config << "\"options\":[";
    for (size_t i = 0; i < options.size(); ++i)
    {
        config << "\"" << escapeJson(options[i]) << "\"";
        if (i < options.size() - 1)
            config << ",";
    }
    config << "],";
    config << "\"availability_topic\":\"eclipse/" << escapeJson(uniqueId) << "/available\",";
    config << "\"device\":" << buildDeviceInfo() << ",";
    config << "\"origin\":" << buildOriginInfo();
    config << "}";

    std::string payload = config.str();

    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "Select discovery payload: %d bytes", (int)payload.length());
    log::dbgLog(logMsg, log::Verbosity::Display, log::Category::IO);

    bool success = mqttClient.publish(topic.c_str(), payload.c_str(), true);

    if (success)
    {
        log::dbgLog("Published select discovery", log::Verbosity::Display, log::Category::IO);
    }
    else
    {
        log::dbgLog("Failed to publish select discovery", log::Verbosity::Error, log::Category::IO);
    }

    return success;
}

bool HomeAssistantDiscovery::removeDiscovery(const char* component, const char* uniqueId)
{
    std::string topic = getDiscoveryTopic(component, uniqueId);
    return mqttClient.publish(topic.c_str(), "", true);
}

std::string HomeAssistantDiscovery::buildDeviceInfo() const
{
    std::ostringstream device;
    device << "{";
    device << "\"identifiers\":[\"" << escapeJson(haConfig.deviceId) << "\"],";
    device << "\"name\":\"" << escapeJson(haConfig.deviceName) << "\",";
    device << "\"manufacturer\":\"" << escapeJson(haConfig.manufacturer) << "\",";
    device << "\"model\":\"" << escapeJson(haConfig.model) << "\",";
    device << "\"sw_version\":\"" << escapeJson(haConfig.swVersion) << "\"";

    // Optional fields
    if (haConfig.hwVersion) {
        device << ",\"hw_version\":\"" << escapeJson(haConfig.hwVersion) << "\"";
    }
    if (haConfig.serialNumber) {
        device << ",\"serial_number\":\"" << escapeJson(haConfig.serialNumber) << "\"";
    }
    if (haConfig.configurationUrl) {
        device << ",\"configuration_url\":\"" << escapeJson(haConfig.configurationUrl) << "\"";
    }

    device << "}";
    return device.str();
}

std::string HomeAssistantDiscovery::buildOriginInfo() const
{
    std::ostringstream origin;
    origin << "{";
    origin << "\"name\":\"" << escapeJson(haConfig.originName) << "\",";
    origin << "\"sw\":\"" << escapeJson(haConfig.originSwVersion) << "\",";
    origin << "\"support_url\":\"" << escapeJson(haConfig.originSupportUrl) << "\"";
    origin << "}";
    return origin.str();
}

std::string HomeAssistantDiscovery::getDiscoveryTopic(const char* component, const char* uniqueId) const
{
    std::ostringstream topic;
    topic << haConfig.discoveryPrefix << "/" << component << "/" 
          << haConfig.deviceId << "/" << uniqueId << "/config";
    return topic.str();
}

std::string HomeAssistantDiscovery::buildDiscoveryPayload(const std::string& config) const
{
    return config;
}

std::string HomeAssistantDiscovery::escapeJson(const std::string& str)
{
    std::ostringstream escaped;
    for (char c : str)
    {
        switch (c)
        {
            case '"':  escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b";  break;
            case '\f': escaped << "\\f";  break;
            case '\n': escaped << "\\n";  break;
            case '\r': escaped << "\\r";  break;
            case '\t': escaped << "\\t";  break;
            default:
                escaped << c;
                break;
        }
    }
    return escaped.str();
}

// EffectListBuilder implementation

EffectListBuilder& EffectListBuilder::add(const std::string& effectName)
{
    effects.push_back(effectName);
    return *this;
}
