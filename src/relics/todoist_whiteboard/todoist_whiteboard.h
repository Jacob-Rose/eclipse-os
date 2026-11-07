// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>
#include <cstdint>

#include <ecore/core.h>
#include <ecore/spsc_queue.h>
#include <ecore/timer.h>
#include <esm/state.h>
#include <esm/state_generic.h>
#include <eio/relic.h>
#include "../wiring.h"
#include <emqtt/mqtt_client.h>
#include <emqtt/mqtt_handler.h>
#include <emqtt/ha_discovery.h>

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
    };

    /* @brief Something MQTT asked for, on its way to the render core.
    * `value` is a WhiteboardPattern, a 0-255 brightness, or 0/1 for power.
    */
    struct WhiteboardCommand
    {
        enum class Kind : uint8_t { Pattern, Brightness, Power };

        Kind kind{Kind::Pattern};
        int value{0};
    };

    /* @brief The whiteboard, split across the two cores.
    *
    * runTick() and everything it calls is the render core's; tickNetwork() and
    * every MQTT handler are the network core's. MQTT reaches the look through
    * a queue of WhiteboardCommands, and the look reaches MQTT through atomics
    * that tickNetwork() publishes when they change, so the render core never
    * touches the network.
    *
    *     relic->runTick();       // loop1()
    *     relic->tickNetwork();   // loop(), after mqttClient->tick()
    */
    class WhiteboardCore : public RelicCore
    {
    public:
        WhiteboardCore(MqttClient& mqtt);

        void setHomeAssistantConfig(const HomeAssistantConfig& config);

        virtual void tick(float deltaTime) override;
        virtual void tickWhileLinked(float deltaTime) override;
        virtual bool handleCommand(string msg) override;

        /// Discovery and state publishing. Network core only.
        void tickNetwork();

        void setPattern(WhiteboardPattern pattern);
        WhiteboardPattern getCurrentPattern() const { return currentPattern; }

    private:
        void setupMQTT();
        void subscribeTopics();
        void publishDiscovery();
        const char* patternName() const;
        void onPatternCommand(const std::string& payload);
        void onBrightnessCommand(const std::string& payload);
        void onPowerCommand(const std::string& payload);
        void onModeCommand(const std::string& payload);
        void applyCommands();
        void queueCommand(WhiteboardCommand::Kind kind, int value);
        void markStateChanged();
        void markModeChanged();
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

        ecore::SpscQueue<WhiteboardCommand, 16> commands;

        // written by the render core, read by the network core
        std::atomic<WhiteboardPattern> currentPattern;
        std::atomic<bool> bPowerOn;
        std::atomic<uint8_t> brightnessByte{0};
        std::atomic<uint32_t> stateRevision{0};
        std::atomic<uint32_t> modeRevision{0};

        // the network core's own
        uint32_t publishedStateRevision{0};
        uint32_t publishedModeRevision{0};
        bool bDiscoveryPublished;
        bool bHasHAConfig;
        bool bWasConnected{false};
    };
}
