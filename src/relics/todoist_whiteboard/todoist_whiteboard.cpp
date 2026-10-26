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
        make_unique<HSVStrip>(wiring::kWhiteboard)
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
            wiring::kWhiteboard.length, 
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

// The topics the light advertises in its discovery config - see
// HomeAssistantDiscovery::publishLightDiscovery, which derives them from the
// unique id. Until now the relic listened on whiteboard/* instead, so the
// light HA showed was never one it could hear; only the Mode select lined up.
static const char* kLightId = "whiteboard_light_01";
static const char* kModeId  = "whiteboard_mode";

static std::string haTopic(const char* id, const char* leaf)
{
    return std::string("eclipse/") + id + "/" + leaf;
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

    // The subscriptions, the availability and the state: everything the
    // broker forgot while we were away.
    subscribeTopics();
    mqttClient.publish(haTopic(kLightId, "available").c_str(), "online", true);
    mqttClient.publish(haTopic(kModeId, "available").c_str(), "online", true);
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

    // The light: on/off, brightness, effect. Plus the older whiteboard/*
    // names, which nothing advertises but an automation may still use.
    mqttHandler.registerHandler(haTopic(kLightId, "set"), [this](const std::string& payload) {
        onPowerCommand(payload);
    });
    mqttHandler.registerHandler(haTopic(kLightId, "brightness/set"), [this](const std::string& payload) {
        onBrightnessCommand(payload);
    });
    mqttHandler.registerHandler(haTopic(kLightId, "effect/set"), [this](const std::string& payload) {
        onPatternCommand(payload);
    });
    mqttHandler.registerHandler("whiteboard/pattern", [this](const std::string& payload) {
        onPatternCommand(payload);
    });
    mqttHandler.registerHandler("whiteboard/brightness", [this](const std::string& payload) {
        onBrightnessCommand(payload);
    });
    mqttHandler.registerHandler("whiteboard/power", [this](const std::string& payload) {
        onPowerCommand(payload);
    });

    // The select.
    mqttHandler.registerHandler(haTopic(kModeId, "set"), [this](const std::string& payload) {
        onModeCommand(payload);
    });

    subscribeTopics();

    dbgLog("WhiteboardCore::setupMQTT complete", Verbosity::Display, Category::Relic);
}

void WhiteboardCore::subscribeTopics()
{
    // Every session starts clean (MQTT_CLEAN_SESSION), so the broker forgets
    // these on every reconnect and they have to be said again - which is why
    // this is not part of setupMQTT, and why tick() calls it on a reconnect.
    mqttClient.subscribe(haTopic(kLightId, "set").c_str());
    mqttClient.subscribe(haTopic(kLightId, "brightness/set").c_str());
    mqttClient.subscribe(haTopic(kLightId, "effect/set").c_str());
    mqttClient.subscribe("whiteboard/pattern");
    mqttClient.subscribe("whiteboard/brightness");
    mqttClient.subscribe("whiteboard/power");
    mqttClient.subscribe(haTopic(kModeId, "set").c_str());
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
    // setPattern has told both entities. Saying it again here put a second,
    // identical echo on the wire after every command, and a checker waiting
    // for the *next* answer would take the stale one.
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
}

const char* WhiteboardCore::patternName() const
{
    switch (currentPattern) {
        case WhiteboardPattern::Noise:     return "noise";
        case WhiteboardPattern::Monocolor: return "monocolor";
        case WhiteboardPattern::Rainbow:   return "rainbow";
        case WhiteboardPattern::Fire:      return "fire";
        default:                           return "noise";
    }
}

void WhiteboardCore::publishState()
{
    const char* power = bPowerOn ? "ON" : "OFF";
    mqttClient.publish(haTopic(kLightId, "state").c_str(), power);
    mqttClient.publish(haTopic(kLightId, "effect").c_str(), patternName());

    // What the slider should sit at: the byte the strip is actually on.
    char brightness[4];
    snprintf(brightness, sizeof(brightness), "%u", getEBrightnessAsByte(coreIO->getGlobalBrightness()));
    mqttClient.publish(haTopic(kLightId, "brightness").c_str(), brightness);

    // The older names, kept for anything already listening.
    mqttClient.publish("whiteboard/state", power);
    mqttClient.publish("whiteboard/pattern/state", patternName());
}

void WhiteboardCore::publishModeState()
{
    mqttClient.publish(haTopic(kModeId, "state").c_str(), patternName());
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

    // Both entities show the pattern, so both hear about it, whichever asked.
    publishModeState();
    publishState();
}

void WhiteboardCore::tick(float deltaTime)
{
    RelicCore::tick(deltaTime);
    stateMachine->tick(deltaTime);

    // A reconnect is a new session: the broker has forgotten our
    // subscriptions and HA may have restarted and forgotten the device. Say
    // it all again. Before this, one WiFi blip on the wall and HA could see
    // the lights but never reach them again.
    const bool connected = mqttClient.isConnected();
    if (connected && !bWasConnected)
    {
        bDiscoveryPublished = false;
    }
    bWasConnected = connected;

    if (bHasHAConfig && !bDiscoveryPublished && connected)
    {
        publishDiscovery();
    }
}

bool WhiteboardCore::handleCommand(string msg)
{
    if (strcmp(msg.c_str(), "next") == 0) {
        int nextPattern = (static_cast<int>(currentPattern) + 1) % static_cast<int>(WhiteboardPattern::MAX);
        setPattern(static_cast<WhiteboardPattern>(nextPattern));
        return true;
    }

    return RelicCore::handleCommand(msg);
}
