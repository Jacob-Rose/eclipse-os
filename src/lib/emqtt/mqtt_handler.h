// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <string>
#include <map>
#include <stdint.h>

#include "../ecore/logging.h"

using namespace ecore;

namespace emqtt
{
    using TopicHandler = std::function<void(const std::string& payload)>;

    class MqttHandler
    {
    public:
        MqttHandler() = default;

        void registerHandler(const std::string& topic, TopicHandler handler);
        void unregisterHandler(const std::string& topic);
        void handleMessage(const char* topic, uint8_t* payload, unsigned int length);
        bool hasHandler(const std::string& topic) const;
        void clearHandlers();

    private:
        std::map<std::string, TopicHandler> handlers;
    };

    class PayloadParser
    {
    public:
        static bool parseBool(const std::string& payload, bool& outValue);
        static bool parseInt(const std::string& payload, int& outValue);
        static bool parseFloat(const std::string& payload, float& outValue);
        static bool parseRGB(const std::string& payload, uint8_t& outR, uint8_t& outG, uint8_t& outB);
        static std::string toLowerCase(const std::string& str);
    };
}
