// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "mqtt_handler.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

using namespace emqtt;

void MqttHandler::registerHandler(const std::string& topic, TopicHandler handler)
{
    handlers[topic] = handler;
    log::dbgLog("Registered MQTT handler for topic", log::Verbosity::Display, log::Category::IO);
}

void MqttHandler::unregisterHandler(const std::string& topic)
{
    handlers.erase(topic);
    log::dbgLog("Unregistered MQTT handler for topic", log::Verbosity::Display, log::Category::IO);
}

void MqttHandler::handleMessage(const char* topic, uint8_t* payload, unsigned int length)
{
    std::string topicStr(topic);
    std::string payloadStr(reinterpret_cast<char*>(payload), length);
    
    auto it = handlers.find(topicStr);
    if (it != handlers.end())
    {
        log::dbgLog("Handling MQTT message", log::Verbosity::Verbose, log::Category::IO);
        it->second(payloadStr);
    }
    else
    {
        log::dbgLog("No handler registered for MQTT topic", log::Verbosity::Warning, log::Category::IO);
    }
}

bool MqttHandler::hasHandler(const std::string& topic) const
{
    return handlers.find(topic) != handlers.end();
}

void MqttHandler::clearHandlers()
{
    handlers.clear();
    log::dbgLog("Cleared all MQTT handlers", log::Verbosity::Display, log::Category::IO);
}

// PayloadParser implementation

bool PayloadParser::parseBool(const std::string& payload, bool& outValue)
{
    std::string lower = toLowerCase(payload);
    
    if (lower == "on" || lower == "true" || lower == "1" || lower == "yes")
    {
        outValue = true;
        return true;
    }
    else if (lower == "off" || lower == "false" || lower == "0" || lower == "no")
    {
        outValue = false;
        return true;
    }
    
    return false;
}

bool PayloadParser::parseInt(const std::string& payload, int& outValue)
{
    if (payload.empty()) return false;
    
    char* endPtr;
    long val = strtol(payload.c_str(), &endPtr, 10);
    
    if (endPtr == payload.c_str() || *endPtr != '\0')
        return false;
    
    outValue = static_cast<int>(val);
    return true;
}

bool PayloadParser::parseFloat(const std::string& payload, float& outValue)
{
    if (payload.empty()) return false;
    
    char* endPtr;
    float val = strtof(payload.c_str(), &endPtr);
    
    if (endPtr == payload.c_str() || *endPtr != '\0')
        return false;
    
    outValue = val;
    return true;
}

bool PayloadParser::parseRGB(const std::string& payload, uint8_t& outR, uint8_t& outG, uint8_t& outB)
{
    size_t pos1 = payload.find(',');
    if (pos1 == std::string::npos)
        return false;
    
    size_t pos2 = payload.find(',', pos1 + 1);
    if (pos2 == std::string::npos)
        return false;
    
    int r, g, b;
    
    if (!parseInt(payload.substr(0, pos1), r))
        return false;
    if (!parseInt(payload.substr(pos1 + 1, pos2 - pos1 - 1), g))
        return false;
    if (!parseInt(payload.substr(pos2 + 1), b))
        return false;
    
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
        return false;
    
    outR = static_cast<uint8_t>(r);
    outG = static_cast<uint8_t>(g);
    outB = static_cast<uint8_t>(b);
    return true;
}

std::string PayloadParser::toLowerCase(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return result;
}
