// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../lib/ecore/core.h"
#include "../../lib/ecore/timer.h"
#include "../../lib/esm/state.h"
#include "../../lib/esm/state_generic.h"
#include "../../lib/eio/relic.h"
#include "../../lib/emqtt/mqtt_client.h"
#include "../../lib/emqtt/mqtt_handler.h"
#include "../../lib/emqtt/ha_discovery.h"

using namespace ecore;
using namespace eio;
using namespace esm;
using namespace emqtt;

namespace todoist_whiteboard
{
    enum class WhiteboardPattern
    {
        Noise,
        Monocolor,
        Rainbow,
        Fire,
        MAX
    };

    class WhiteboardIO : public RelicIO
    {
    public:
        WhiteboardIO();
        virtual void init() override;

        uint16_t stripLEDPin = 13;
        uint16_t stripLength = 60;
    };

    class WhiteboardCore : public RelicCore
    {
    public:
        WhiteboardCore(MqttClient& mqtt);

        void setHomeAssistantConfig(const HomeAssistantConfig& config);

        virtual void tick(float deltaTime) override;
        virtual bool handleCommand(string msg) override;

        void setPattern(WhiteboardPattern pattern);
        WhiteboardPattern getCurrentPattern() const { return currentPattern; }

    private:
        void setupMQTT();
        void publishDiscovery();
        void onPatternCommand(const std::string& payload);
        void onBrightnessCommand(const std::string& payload);
        void onPowerCommand(const std::string& payload);
        void onModeCommand(const std::string& payload);
        void publishState();
        void publishModeState();

        std::unique_ptr<StateMachine_GenericHSV> stateMachine{ nullptr };
        std::unique_ptr<StateManager> stateManager{ nullptr };

        shared_ptr<State_GenericHSV> noiseState;
        shared_ptr<State_GenericHSV> monoState;
        shared_ptr<State_GenericHSV> rainbowState;
        shared_ptr<State_GenericHSV> fireState;

        MqttClient& mqttClient;
        MqttHandler mqttHandler;
        std::unique_ptr<HomeAssistantDiscovery> haDiscovery;
        HomeAssistantConfig haConfig;

        WhiteboardPattern currentPattern;
        bool bPowerOn;
        bool bDiscoveryPublished;
        bool bHasHAConfig;
    };
}
