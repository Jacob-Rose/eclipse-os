// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "todoist_whiteboard.h"

#include <string>

#include "../../lib/eio/strip_projection.h"
#include "../../lib/ecore/logging.h"
#include "../../lib/eanim/noise.h"
#include "../../lib/eanim/lfo.h"
#include "../../kits/palettes.h"

using namespace todoist_whiteboard;
using namespace ecore;
using namespace ecore::log;
using namespace eio;
using namespace eanim;

WhiteboardIO::WhiteboardIO() : RelicIO()
{
}

void WhiteboardIO::init()
{
    RelicIO::init();

    auto [mainStripIt, stripInserted] = strips.emplace(
        static_cast<uint8_t>(0), 
        make_unique<HSVStrip>(stripLength, stripLEDPin)
    );

    HSVStrip* mainStrip = mainStripIt->second.get();

    auto [segmentIt, segmentInserted] = strip_segments.emplace(
        0, 
        make_unique<HSVStripSegment>(mainStrip, 0)
    );
    
    if (segmentInserted) {
        HSVStripNodeFactory::GenerateAxisRow(
            segmentIt->second.get(), 
            0, 
            stripLength, 
            Coord(0, 0), 
            Coord(1.f, 0)
        );
    }

    dbgLog("WhiteboardIO::init", Verbosity::Verbose, Category::Relic);
    setGlobalBrightness(EBrightness::HIGH);
}

class Pattern_Whiteboard_Noise : public GeneratorHSV
{
public:
    Pattern_Whiteboard_Noise()
    {
        coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        coreNoise.noise.SetFrequency(0.1f);
        coreNoise.timeScale = 0.5f;
        coreNoise.imageScaleX = 1.0f;
        coreNoise.imageScaleY = 1.0f;
    }

    PerlinNoiseGenerator2D coreNoise;
    HSVPalette palette {HSV(309.0f, 0.92f, 0.98f), HSV(255.0f, 1.0f, 0.39f)};

    virtual void tick(float deltaTime) override
    {
        coreNoise.tick(deltaTime);
    }

    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override
    {
        HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
        float noiseAlpha = coreNoise.evaluate(castedNode->coord.x, castedNode->coord.y);
        inOutColor = palette.getColor(noiseAlpha);
    }
};

class Pattern_Whiteboard_Monocolor : public GeneratorHSV
{
public:
    HSV color = HSV(180.0f, 0.8f, 0.8f);

    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override
    {
        inOutColor = color;
    }
};

class Pattern_Whiteboard_Rainbow : public GeneratorHSV
{
public:
    Pattern_Whiteboard_Rainbow()
    {
        lfo.speed = 0.2f;
        lfo.width = 5.0f;
    }

    LFO lfo;

    virtual void tick(float deltaTime) override
    {
        lfo.tick(deltaTime);
    }

    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override
    {
        HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
        float hue = lfo.evaluate(castedNode->coord.x) * 360.0f;
        inOutColor = HSV(hue, 1.0f, 1.0f);
    }
};

class Pattern_Whiteboard_Fire : public GeneratorHSV
{
public:
    Pattern_Whiteboard_Fire()
    {
        coreNoise.noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        coreNoise.noise.SetFrequency(0.15f);
        coreNoise.timeScale = 1.0f;
    }

    PerlinNoiseGenerator2D coreNoise;
    HSVPalette palette {HSV(15.0f, 0.94f, 0.98f), HSV(343.0f, 1.0f, 0.78f), HSV(337.0f, 0.92f, 0.57f)};

    virtual void tick(float deltaTime) override
    {
        coreNoise.tick(deltaTime);
    }

    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override
    {
        HSVStripNode_Mapped2D* castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);
        float noiseAlpha = coreNoise.evaluate(castedNode->coord.x, castedNode->coord.y);
        inOutColor = palette.getColor(noiseAlpha);
    }
};

WhiteboardCore::WhiteboardCore(MqttClient& mqtt)
    : RelicCore(),
      mqttClient(mqtt),
      currentPattern(WhiteboardPattern::Noise),
      bPowerOn(true),
      bDiscoveryPublished(false),
      bHasHAConfig(false)
{
    coreIO = std::make_unique<WhiteboardIO>();
    coreIO->init();

    stateMachine = std::make_unique<StateMachine_GenericHSV>();
    stateMachine->setRelicIO(coreIO.get());
    stateManager = std::make_unique<StateManager>();

    noiseState = std::make_shared<State_GenericHSV>("noiseState", coreIO.get());
    noiseState->setGenerator(make_shared<Pattern_Whiteboard_Noise>());
    noiseState->init();

    monoState = std::make_shared<State_GenericHSV>("monoState", coreIO.get());
    monoState->setGenerator(make_shared<Pattern_Whiteboard_Monocolor>());
    monoState->init();

    rainbowState = std::make_shared<State_GenericHSV>("rainbowState", coreIO.get());
    rainbowState->setGenerator(std::make_shared<Pattern_Whiteboard_Rainbow>());
    rainbowState->init();

    fireState = std::make_shared<State_GenericHSV>("fireState", coreIO.get());
    fireState->setGenerator(std::make_shared<Pattern_Whiteboard_Fire>());
    fireState->init();

    stateManager->addState(noiseState);
    stateManager->addState(monoState);
    stateManager->addState(rainbowState);
    stateManager->addState(fireState);

    stateMachine->setActiveState(noiseState);
    stateMachine->init();
}

void WhiteboardCore::setHomeAssistantConfig(const HomeAssistantConfig& config)
{
    haConfig = config;
    bHasHAConfig = true;

    setupMQTT();

    // Initialize HA discovery with config
    haDiscovery = std::make_unique<HomeAssistantDiscovery>(mqttClient);
    haDiscovery->init(haConfig);

    // Try to publish discovery
    publishDiscovery();

    dbgLog("WhiteboardCore::init complete", Verbosity::Display, Category::Relic);
}

void WhiteboardCore::publishDiscovery()
{
    if (!bHasHAConfig || !haDiscovery)
    {
        dbgLog("HA config not set, skipping discovery", Verbosity::Warning, Category::Relic);
        return;
    }

    if (!mqttClient.isConnected())
    {
        dbgLog("MQTT not connected, will retry discovery on reconnect", Verbosity::Warning, Category::Relic);
        bDiscoveryPublished = false;
        return;
    }

    dbgLog("Publishing HA discovery messages", Verbosity::Display, Category::Relic);

    // Publish light entity with effects
    std::vector<std::string> effects = {"noise", "monocolor", "rainbow", "fire"};
    haDiscovery->publishLightDiscovery("whiteboard_light_01", "Todoist Whiteboard", effects);

    // Publish select entity for mode control
    std::vector<std::string> modes = {"noise", "monocolor", "rainbow", "fire"};
    haDiscovery->publishSelectDiscovery("whiteboard_mode", "Whiteboard Mode", modes);

    publishState();
    publishModeState();

    bDiscoveryPublished = true;
    dbgLog("HA discovery published successfully", Verbosity::Display, Category::Relic);
}

void WhiteboardCore::setupMQTT()
{
    mqttClient.setCallback([this](const char* topic, uint8_t* payload, unsigned int length) {
        mqttHandler.handleMessage(topic, payload, length);
    });

    mqttClient.subscribe("whiteboard/pattern");
    mqttClient.subscribe("whiteboard/brightness");
    mqttClient.subscribe("whiteboard/power");

    mqttHandler.registerHandler("whiteboard/pattern", [this](const std::string& payload) {
        onPatternCommand(payload);
    });

    mqttHandler.registerHandler("whiteboard/brightness", [this](const std::string& payload) {
        onBrightnessCommand(payload);
    });

    mqttHandler.registerHandler("whiteboard/power", [this](const std::string& payload) {
        onPowerCommand(payload);
    });

    // Subscribe to mode command topic
    mqttClient.subscribe("eclipse/whiteboard_mode/set");
    mqttHandler.registerHandler("eclipse/whiteboard_mode/set", [this](const std::string& payload) {
        onModeCommand(payload);
    });

    dbgLog("WhiteboardCore::setupMQTT complete", Verbosity::Display, Category::Relic);
}

void WhiteboardCore::onPatternCommand(const std::string& payload)
{
    dbgLog(("Pattern command: " + payload).c_str(), Verbosity::Display, Category::Relic);

    if (payload == "noise") {
        setPattern(WhiteboardPattern::Noise);
    } else if (payload == "monocolor") {
        setPattern(WhiteboardPattern::Monocolor);
    } else if (payload == "rainbow") {
        setPattern(WhiteboardPattern::Rainbow);
    } else if (payload == "fire") {
        setPattern(WhiteboardPattern::Fire);
    }

    publishState();
    // Note: publishModeState() is already called in setPattern()
}

void WhiteboardCore::onBrightnessCommand(const std::string& payload)
{
    int brightness;
    if (!PayloadParser::parseInt(payload, brightness))
        return;
    
    dbgLog(("Brightness command: " + std::to_string(brightness)).c_str(), Verbosity::Display, Category::Relic);

    if (brightness >= 192)
        coreIO->setGlobalBrightness(EBrightness::HIGH);
    else if (brightness >= 64)
        coreIO->setGlobalBrightness(EBrightness::MED);
    else
        coreIO->setGlobalBrightness(EBrightness::MIN);

    publishState();
}

void WhiteboardCore::onPowerCommand(const std::string& payload)
{
    dbgLog(("Power command: " + payload).c_str(), Verbosity::Display, Category::Relic);

    bool newPowerState;
    if (!PayloadParser::parseBool(payload, newPowerState))
        return;

    bPowerOn = newPowerState;

    if (bPowerOn) {
        coreIO->setGlobalBrightness(EBrightness::HIGH);
    } else {
        coreIO->setGlobalBrightness(EBrightness::NIGHTTRIP);
    }

    publishState();
}

void WhiteboardCore::onModeCommand(const std::string& payload)
{
    dbgLog(("Mode command: " + payload).c_str(), Verbosity::Display, Category::Relic);

    if (payload == "noise") {
        setPattern(WhiteboardPattern::Noise);
    } else if (payload == "monocolor") {
        setPattern(WhiteboardPattern::Monocolor);
    } else if (payload == "rainbow") {
        setPattern(WhiteboardPattern::Rainbow);
    } else if (payload == "fire") {
        setPattern(WhiteboardPattern::Fire);
    }

    publishModeState();
}

void WhiteboardCore::publishState()
{
    mqttClient.publish("whiteboard/state", bPowerOn ? "ON" : "OFF");
    
    std::string patternName;
    switch (currentPattern) {
        case WhiteboardPattern::Noise:
            patternName = "noise";
            break;
        case WhiteboardPattern::Monocolor:
            patternName = "monocolor";
            break;
        case WhiteboardPattern::Rainbow:
            patternName = "rainbow";
            break;
        case WhiteboardPattern::Fire:
            patternName = "fire";
            break;
        default:
            patternName = "unknown";
            break;
    }
    
    mqttClient.publish("whiteboard/pattern/state", patternName.c_str());
}

void WhiteboardCore::publishModeState()
{
    std::string modeName;
    switch (currentPattern) {
        case WhiteboardPattern::Noise:
            modeName = "noise";
            break;
        case WhiteboardPattern::Monocolor:
            modeName = "monocolor";
            break;
        case WhiteboardPattern::Rainbow:
            modeName = "rainbow";
            break;
        case WhiteboardPattern::Fire:
            modeName = "fire";
            break;
        default:
            modeName = "noise";
            break;
    }

    mqttClient.publish("eclipse/whiteboard_mode/state", modeName.c_str());
}

void WhiteboardCore::setPattern(WhiteboardPattern pattern)
{
    currentPattern = pattern;

    switch (pattern) {
        case WhiteboardPattern::Noise:
            stateMachine->setNextState(noiseState);
            break;
        case WhiteboardPattern::Monocolor:
            stateMachine->setNextState(monoState);
            break;
        case WhiteboardPattern::Rainbow:
            stateMachine->setNextState(rainbowState);
            break;
        case WhiteboardPattern::Fire:
            stateMachine->setNextState(fireState);
            break;
        default:
            break;
    }

    dbgLog(("Pattern switched to: " + std::to_string(static_cast<int>(pattern))).c_str(), Verbosity::Display, Category::Relic);
    publishModeState();
}

void WhiteboardCore::tick(float deltaTime)
{
    RelicCore::tick(deltaTime);
    stateMachine->tick(deltaTime);

    // Republish discovery if MQTT reconnected and we haven't published yet
    if (bHasHAConfig && !bDiscoveryPublished && mqttClient.isConnected())
    {
        publishDiscovery();
    }
}

bool WhiteboardCore::handleCommand(string msg)
{
    if (strcmp(msg.c_str(), "next") == 0) {
        int nextPattern = (static_cast<int>(currentPattern) + 1) % static_cast<int>(WhiteboardPattern::MAX);
        setPattern(static_cast<WhiteboardPattern>(nextPattern));
        publishState();
        return true;
    }

    return RelicCore::handleCommand(msg);
}
