// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "todoist_whiteboard.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include <eio/strip_projection.h>
#include <ecore/logging.h>
#include <ecore/math.h>
#include <eanim/noise.h>
#include <eanim/lfo.h>
#include <eanim/particles.h>
#include "../../kits/palettes.h"

// The strip's 2d map, exported from tools/mapper/mapper.html as
// whiteboard_map.h into this folder. Without one the lights are a row, one
// unit a pixel.
#if __has_include("whiteboard_map.h")
#include "whiteboard_map.h"
#define WHITEBOARD_HAS_MAP 1
#else
#define WHITEBOARD_HAS_MAP 0
#endif

using namespace todoist_whiteboard;
using namespace ecore;
using namespace ecore::log;
using namespace eio;
using namespace eanim;

namespace
{
    /// What the strip runs at when Home Assistant says 255.
    constexpr uint8_t kFullStripBrightness = 63;

    constexpr float kBrightnessFadeSeconds = 0.75f;

    constexpr uint32_t kDefaultColorRgb = 0xFA14D7;
    constexpr uint32_t kDefaultAccentRgb = 0x190063;

    /// By WhiteboardPattern. What HA shows, and what it sends back.
    constexpr const char* kPatternNames[] = {"noise", "monocolor", "rainbow-christmas", "rainbow-gradient", "fire",
                                           "matrix-rainbow", "matrix-color"};
    static_assert(sizeof(kPatternNames) / sizeof(kPatternNames[0]) == static_cast<size_t>(WhiteboardPattern::MAX),
                  "a name for every pattern");

    HSV colorFromRgb(uint32_t rgb)
    {
        return HSV::fromRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }

    /// The extent of the whiteboard's map, in node coordinates.
    struct MapBounds
    {
        float minX{0.0f}, maxX{1.0f}, minY{0.0f}, maxY{1.0f};
    };

    MapBounds mapBounds(const RelicIO& io)
    {
        MapBounds bounds{INFINITY, -INFINITY, INFINITY, -INFINITY};
        for (const auto& segment : io.strip_segments)
        {
            for (const auto& node : segment.second->getNodes())
            {
                if (node->GetStripNodeType() != StripNodeType::MAPPED2D) continue;
                const Coord& at = static_cast<const HSVStripNode_Mapped2D*>(node.get())->coord;
                bounds.minX = std::min(bounds.minX, at.x); bounds.maxX = std::max(bounds.maxX, at.x);
                bounds.minY = std::min(bounds.minY, at.y); bounds.maxY = std::max(bounds.maxY, at.y);
            }
        }
        if (!(bounds.maxX > bounds.minX)) { bounds.minX = 0.0f; bounds.maxX = 1.0f; }
        if (!(bounds.maxY > bounds.minY)) { bounds.minY = 0.0f; bounds.maxY = 1.0f; }
        return bounds;
    }

    std::vector<std::string> patternNames()
    {
        return std::vector<std::string>(std::begin(kPatternNames), std::end(kPatternNames));
    }
}

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
#if WHITEBOARD_HAS_MAP
        static_assert(std::size(mapping::kWhiteboard) == wiring::kWhiteboard.length,
                      "whiteboard_map.h and relics/wiring.h disagree on the strip length: re-export the map");
        // scaled to one unit a pixel, so the noise looks keep the density they had on the row
        HSVStripNodeFactory::GenerateFromTable(segmentIt->second.get(), 0, mapping::kWhiteboard, wiring::kWhiteboard.length);
        dbgLog("WhiteboardIO::init: 2d map from whiteboard_map.h", Verbosity::Display, Category::Relic);
#else
        HSVStripNodeFactory::GenerateAxisRow(
            segmentIt->second.get(), 
            0, 
            wiring::kWhiteboard.length, 
            Coord(0, 0), 
            Coord(1.f, 0)
        );
#endif
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
    HSVPalette palette {colorFromRgb(kDefaultColorRgb), colorFromRgb(kDefaultAccentRgb)};

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

class Pattern_Whiteboard_RainbowChristmas : public GeneratorHSV
{
public:
    Pattern_Whiteboard_RainbowChristmas()
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
        // along the wiring, not across the wall: neighbours alternate however the strip is hung
        float hue = lfo.evaluate(static_cast<float>(inNode->getStripIdx())) * 360.0f;
        inOutColor = HSV(hue, 1.0f, 1.0f);
    }
};

// The hue wheel laid across the board, left to right, scrolling right to left.
class Pattern_Whiteboard_RainbowGradient : public GeneratorHSV
{
public:
    /// boards a second; negative moves the rainbow right to left
    static constexpr float kSpeed = -1.0f / 22.5f;

    explicit Pattern_Whiteboard_RainbowGradient(const MapBounds& inBounds) : bounds(inBounds) {}

    float offset = 0.0f; // 0-1, and it wraps, so it stays precise however long it runs

    virtual void tick(float deltaTime) override
    {
        offset = std::fmod(offset + kSpeed * deltaTime + 1.0f, 1.0f);
    }

    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override
    {
        const Coord& at = static_cast<HSVStripNode_Mapped2D*>(inNode)->coord;
        const float across = (at.x - bounds.minX) / (bounds.maxX - bounds.minX);
        inOutColor = HSV(std::fmod(across - offset + 1.0f, 1.0f) * 360.0f, 1.0f, 1.0f);
    }

private:
    MapBounds bounds;
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

/* @brief Digital rain down the wall: drops are particles that spawn above
* the board at random across it and fall straight down, each a pale head and
* a tail that fades above it, on black. Distances are node units, about one a
* pixel.
*
* A subclass says what colour a drop is from its own random 0-1 pick; the
* head is always paler than that colour.
*/
class Pattern_Whiteboard_Matrix : public GeneratorHSV
{
public:
    static constexpr int kMaxDrops = 24;
    static constexpr float kDropsPerSecond = 5.0f;
    static constexpr float kColumnHalfWidth = 1.1f;   // catches the up and down run of one column
    static constexpr float kMinSpeed = 4.0f, kMaxSpeed = 9.0f;   // units a second
    static constexpr float kMinTail = 3.0f, kMaxTail = 7.0f;     // units
    static constexpr float kHeadLength = 0.8f;

    explicit Pattern_Whiteboard_Matrix(const MapBounds& inBounds) : bounds(inBounds), drops(kMaxDrops)
    {
        // a board's worth already falling, so the first second is not empty
        const float height = bounds.maxY - bounds.minY;
        for (int idx = 0; idx < kMaxDrops / 2; ++idx)
        {
            spawnDrop(get_random_float_in_range(bounds.minY, bounds.maxY + height * 0.5f));
        }
    }

    virtual void tick(float deltaTime) override
    {
        drops.tick(deltaTime);

        spawnDebt += kDropsPerSecond * deltaTime;
        while (spawnDebt >= 1.0f)
        {
            spawnDebt -= 1.0f;
            spawnDrop(bounds.maxY + get_random_float_in_range(0.0f, kMaxTail));
        }
    }

    virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override
    {
        const Coord& at = static_cast<HSVStripNode_Mapped2D*>(inNode)->coord;

        float best = 0.0f;
        const Particle* lit = nullptr;
        for (const Particle& drop : drops.all())
        {
            if (!drop.bAlive || std::fabs(at.x - drop.position.x) > drop.radius) continue;
            const float tail = tailOf(drop);
            const float behind = at.y - drop.position.y;   // the tail is above the head
            if (behind < -kHeadLength * 0.5f || behind > tail) continue;
            const float fade = 1.0f - std::max(0.0f, behind) / tail;
            const float level = fade * fade;
            if (level > best) { best = level; lit = &drop; }
        }

        if (lit == nullptr)
        {
            inOutColor = HSV(0.0f, 0.0f, 0.0f);
            return;
        }

        const HSV colour = dropColor(valueOf(*lit));
        const bool head = at.y - lit->position.y < kHeadLength;
        inOutColor = HSV(colour.getHueFloat(), colour.getSatFloat() * (head ? 0.35f : 1.0f),
                         colour.getValFloat() * best);
    }

protected:
    /// The drop's colour from its random 0-1 pick, fixed for its fall.
    virtual HSV dropColor(float value) const = 0;

private:
    static float valueOf(const Particle& drop) { return hash01(drop.seed); }
    static float tailOf(const Particle& drop) { return kMinTail + hash01(drop.seed ^ 0x9E3779B9u) * (kMaxTail - kMinTail); }

    void spawnDrop(float y)
    {
        Particle* drop = drops.spawn();
        if (drop == nullptr) return;

        const float speed = get_random_float_in_range(kMinSpeed, kMaxSpeed);
        drop->position = Coord(get_random_float_in_range(bounds.minX, bounds.maxX), y);
        drop->velocity = Coord(0.0f, -speed);
        drop->radius = kColumnHalfWidth;
        // gone once the tail has cleared the foot of the board
        drop->lifetime = (y - bounds.minY + tailOf(*drop)) / speed;
    }

    MapBounds bounds;
    ParticleSystem drops;
    float spawnDebt = 0.0f;
};

// Matrix rain where each drop picks a random hue.
class Pattern_Whiteboard_MatrixRainbow : public Pattern_Whiteboard_Matrix
{
public:
    using Pattern_Whiteboard_Matrix::Pattern_Whiteboard_Matrix;

protected:
    virtual HSV dropColor(float value) const override
    {
        return HSV(value * 360.0f, 1.0f, 1.0f);
    }
};

// Matrix rain in the programmed colours: each drop picks a random point on
// the main -> accent gradient and keeps it all the way down.
class Pattern_Whiteboard_MatrixColor : public Pattern_Whiteboard_Matrix
{
public:
    using Pattern_Whiteboard_Matrix::Pattern_Whiteboard_Matrix;

    HSVPalette palette {colorFromRgb(kDefaultColorRgb), colorFromRgb(kDefaultAccentRgb)};

protected:
    virtual HSV dropColor(float value) const override
    {
        return palette.getColor(value);
    }
};

WhiteboardCore::WhiteboardCore(MqttClient& mqtt)
    : RelicCore(),
      mqttClient(mqtt),
      currentPattern(WhiteboardPattern::Noise),
      bPowerOn(true),
      colorRgb(kDefaultColorRgb),
      accentRgb(kDefaultAccentRgb),
      bDiscoveryPublished(false),
      bHasHAConfig(false)
{
    coreIO = std::make_unique<WhiteboardIO>();
    coreIO->init();

    stateMachine = std::make_unique<StateMachine_GenericHSV>();
    stateMachine->setRelicIO(coreIO.get());
    stateManager = std::make_unique<StateManager>();

    noiseState = std::make_shared<State_GenericHSV>("noiseState", coreIO.get());
    noisePattern = make_shared<Pattern_Whiteboard_Noise>();
    noiseState->setGenerator(noisePattern);
    noiseState->init();

    monoState = std::make_shared<State_GenericHSV>("monoState", coreIO.get());
    monoPattern = make_shared<Pattern_Whiteboard_Monocolor>();
    monoPattern->color = colorFromRgb(kDefaultColorRgb);
    monoState->setGenerator(monoPattern);
    monoState->init();

    rainbowChristmasState = std::make_shared<State_GenericHSV>("rainbowChristmasState", coreIO.get());
    rainbowChristmasState->setGenerator(std::make_shared<Pattern_Whiteboard_RainbowChristmas>());
    rainbowChristmasState->init();

    const MapBounds bounds = mapBounds(*coreIO);

    rainbowGradientState = std::make_shared<State_GenericHSV>("rainbowGradientState", coreIO.get());
    rainbowGradientState->setGenerator(std::make_shared<Pattern_Whiteboard_RainbowGradient>(bounds));
    rainbowGradientState->init();

    fireState = std::make_shared<State_GenericHSV>("fireState", coreIO.get());
    fireState->setGenerator(std::make_shared<Pattern_Whiteboard_Fire>());
    fireState->init();

    matrixRainbowState = std::make_shared<State_GenericHSV>("matrixRainbowState", coreIO.get());
    matrixRainbowState->setGenerator(std::make_shared<Pattern_Whiteboard_MatrixRainbow>(bounds));
    matrixRainbowState->init();

    matrixColorState = std::make_shared<State_GenericHSV>("matrixColorState", coreIO.get());
    matrixColorPattern = std::make_shared<Pattern_Whiteboard_MatrixColor>(bounds);
    matrixColorState->setGenerator(matrixColorPattern);
    matrixColorState->init();

    stateManager->addState(noiseState);
    stateManager->addState(monoState);
    stateManager->addState(rainbowChristmasState);
    stateManager->addState(rainbowGradientState);
    stateManager->addState(fireState);
    stateManager->addState(matrixRainbowState);
    stateManager->addState(matrixColorState);

    stateMachine->setActiveState(noiseState);
    stateMachine->init();

    stripFade.setEasing(getEasingFunction(easing_functions::EaseInOutCubic));
    stripFade.reset(kFullStripBrightness);
    appliedStripBrightness = getEBrightnessAsByte(coreIO->getGlobalBrightness());
}

// The topics the light advertises in its discovery config - see
// HomeAssistantDiscovery::publishLightDiscovery, which derives them from the
// unique id. Until now the relic listened on whiteboard/* instead, so the
// light HA showed was never one it could hear; only the Mode select lined up.
static const char* kLightId = "whiteboard_light_01";
static const char* kModeId  = "whiteboard_mode";
static const char* kAccentId = "whiteboard_accent_01";

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
    haDiscovery->publishLightDiscovery(kLightId, "Todoist Whiteboard", patternNames());

    // HA has no colour entity of its own, so the accent is a light
    haDiscovery->publishColorDiscovery(kAccentId, "Accent Color");

    // Publish select entity for mode control
    haDiscovery->publishSelectDiscovery(kModeId, "Whiteboard Mode", patternNames());

    publishedStateRevision = stateRevision.load(std::memory_order_acquire);
    publishedModeRevision = modeRevision.load(std::memory_order_acquire);
    publishedAccentRevision = accentRevision.load(std::memory_order_acquire);

    // The subscriptions, the availability and the state: everything the
    // broker forgot while we were away.
    subscribeTopics();
    mqttClient.publish(haTopic(kLightId, "available").c_str(), "online", true);
    mqttClient.publish(haTopic(kModeId, "available").c_str(), "online", true);
    mqttClient.publish(haTopic(kAccentId, "available").c_str(), "online", true);
    publishState();
    publishModeState();
    publishAccentState();

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
    mqttHandler.registerHandler(haTopic(kLightId, "rgb/set"), [this](const std::string& payload) {
        onColorCommand(payload);
    });
    mqttHandler.registerHandler(haTopic(kAccentId, "rgb/set"), [this](const std::string& payload) {
        onAccentColorCommand(payload);
    });
    mqttHandler.registerHandler(haTopic(kAccentId, "set"), [this](const std::string& payload) {
        onAccentPowerCommand(payload);
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
    mqttClient.subscribe(haTopic(kLightId, "rgb/set").c_str());
    mqttClient.subscribe(haTopic(kAccentId, "rgb/set").c_str());
    mqttClient.subscribe(haTopic(kAccentId, "set").c_str());
    mqttClient.subscribe("whiteboard/pattern");
    mqttClient.subscribe("whiteboard/brightness");
    mqttClient.subscribe("whiteboard/power");
    mqttClient.subscribe(haTopic(kModeId, "set").c_str());
}

void WhiteboardCore::queueCommand(WhiteboardCommand::Kind kind, int value)
{
    WhiteboardCommand command;
    command.kind = kind;
    command.value = value;
    if (!commands.push(command))
    {
        dbgLog("Command queue full, dropping a command", Verbosity::Warning, Category::Relic);
    }
}

void WhiteboardCore::onPatternCommand(const std::string& payload)
{
    dbgLog(("Pattern command: " + payload).c_str(), Verbosity::Display, Category::Relic);

    // "rainbow" is the name before rainbow-christmas, for anything still using it
    const std::string name = payload == "rainbow" ? "rainbow-christmas" : payload;

    const auto found = std::find(std::begin(kPatternNames), std::end(kPatternNames), name);
    if (found == std::end(kPatternNames))
    {
        return;
    }
    queueCommand(WhiteboardCommand::Kind::Pattern, static_cast<int>(found - std::begin(kPatternNames)));
}

void WhiteboardCore::onBrightnessCommand(const std::string& payload)
{
    int brightness;
    if (!PayloadParser::parseInt(payload, brightness))
        return;

    dbgLog(("Brightness command: " + std::to_string(brightness)).c_str(), Verbosity::Display, Category::Relic);
    queueCommand(WhiteboardCommand::Kind::Brightness, brightness);
}

void WhiteboardCore::onPowerCommand(const std::string& payload)
{
    dbgLog(("Power command: " + payload).c_str(), Verbosity::Display, Category::Relic);

    bool newPowerState;
    if (!PayloadParser::parseBool(payload, newPowerState))
        return;

    queueCommand(WhiteboardCommand::Kind::Power, newPowerState ? 1 : 0);
}

void WhiteboardCore::onModeCommand(const std::string& payload)
{
    onPatternCommand(payload);
}

void WhiteboardCore::onColorCommand(const std::string& payload)
{
    dbgLog(("Color command: " + payload).c_str(), Verbosity::Display, Category::Relic);

    uint8_t red, green, blue;
    if (!PayloadParser::parseRGB(payload, red, green, blue))
        return;

    queueCommand(WhiteboardCommand::Kind::Color, (red << 16) | (green << 8) | blue);
}

void WhiteboardCore::onAccentColorCommand(const std::string& payload)
{
    dbgLog(("Accent color command: " + payload).c_str(), Verbosity::Display, Category::Relic);

    uint8_t red, green, blue;
    if (!PayloadParser::parseRGB(payload, red, green, blue))
        return;

    queueCommand(WhiteboardCommand::Kind::AccentColor, (red << 16) | (green << 8) | blue);
}

void WhiteboardCore::onAccentPowerCommand(const std::string& payload)
{
    bool enabled;
    if (!PayloadParser::parseBool(payload, enabled))
        return;

    queueCommand(WhiteboardCommand::Kind::AccentPower, enabled ? 1 : 0);
}

void WhiteboardCore::updateNoiseColors()
{
    const HSV main = colorFromRgb(colorRgb.load());
    noisePattern->palette = HSVPalette{main, bAccentOn.load() ? colorFromRgb(accentRgb.load()) : main};
    matrixColorPattern->palette = noisePattern->palette;
}

void WhiteboardCore::applyCommands()
{
    WhiteboardCommand command;
    while (commands.pop(command))
    {
        switch (command.kind)
        {
            case WhiteboardCommand::Kind::Pattern:
                setPattern(static_cast<WhiteboardPattern>(command.value));
                break;

            case WhiteboardCommand::Kind::Brightness:
                // HA sends a brightness and then ON, so a level alone does not
                // light a board that is off. Zero is how a slider says off.
                if (command.value > 0)
                    brightnessLevel.store(static_cast<uint8_t>(std::min(command.value, 255)));
                else
                    bPowerOn.store(false);
                startBrightnessFade();
                markStateChanged();
                break;

            case WhiteboardCommand::Kind::Power:
                bPowerOn.store(command.value != 0);
                startBrightnessFade();
                markStateChanged();
                break;

            case WhiteboardCommand::Kind::Color:
                colorRgb.store(static_cast<uint32_t>(command.value));
                monoPattern->color = colorFromRgb(static_cast<uint32_t>(command.value));
                updateNoiseColors();
                markStateChanged();
                break;

            case WhiteboardCommand::Kind::AccentColor:
                accentRgb.store(static_cast<uint32_t>(command.value));
                updateNoiseColors();
                markAccentChanged();
                break;

            case WhiteboardCommand::Kind::AccentPower:
                bAccentOn.store(command.value != 0);
                updateNoiseColors();
                markAccentChanged();
                break;
        }
    }
}

void WhiteboardCore::startBrightnessFade()
{
    float target = 0.0f;
    if (bPowerOn.load())
    {
        // Never rounded down to dark: only off is off.
        target = std::max(1.0f, std::round(brightnessLevel.load() * kFullStripBrightness / 255.0f));
    }
    stripFade.startLerp(target, kBrightnessFadeSeconds);
}

void WhiteboardCore::tickBrightness(float deltaTime)
{
    stripFade.tick(deltaTime);

    const uint8_t brightness = static_cast<uint8_t>(std::lround(stripFade.getValue()));
    if (brightness != appliedStripBrightness)
    {
        appliedStripBrightness = brightness;
        for (const auto& strip : coreIO->strips)
        {
            strip.second->setStripBrightness(brightness);
        }
    }
}

void WhiteboardCore::markStateChanged()
{
    stateRevision.store(stateRevision.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}

void WhiteboardCore::markAccentChanged()
{
    accentRevision.store(accentRevision.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}

void WhiteboardCore::markModeChanged()
{
    modeRevision.store(modeRevision.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}

const char* WhiteboardCore::patternName() const
{
    return kPatternNames[static_cast<int>(currentPattern.load())];
}

static std::string rgbPayload(uint32_t rgb)
{
    char text[12];
    snprintf(text, sizeof(text), "%u,%u,%u", static_cast<unsigned>((rgb >> 16) & 0xFF),
             static_cast<unsigned>((rgb >> 8) & 0xFF), static_cast<unsigned>(rgb & 0xFF));
    return text;
}

void WhiteboardCore::publishState()
{
    // Retained, so HA has them whenever it subscribes: the first ones go out
    // alongside discovery, before it is listening.
    const char* power = bPowerOn.load() ? "ON" : "OFF";
    mqttClient.publish(haTopic(kLightId, "state").c_str(), power, true);
    mqttClient.publish(haTopic(kLightId, "effect").c_str(), patternName(), true);

    char brightness[4];
    snprintf(brightness, sizeof(brightness), "%u", static_cast<unsigned>(brightnessLevel.load()));
    mqttClient.publish(haTopic(kLightId, "brightness").c_str(), brightness, true);

    mqttClient.publish(haTopic(kLightId, "rgb").c_str(), rgbPayload(colorRgb.load()).c_str(), true);

    // The older names, kept for anything already listening.
    mqttClient.publish("whiteboard/state", power);
    mqttClient.publish("whiteboard/pattern/state", patternName());
}

void WhiteboardCore::publishAccentState()
{
    mqttClient.publish(haTopic(kAccentId, "state").c_str(), bAccentOn.load() ? "ON" : "OFF", true);
    mqttClient.publish(haTopic(kAccentId, "rgb").c_str(), rgbPayload(accentRgb.load()).c_str(), true);
}

void WhiteboardCore::publishModeState()
{
    mqttClient.publish(haTopic(kModeId, "state").c_str(), patternName(), true);
}

void WhiteboardCore::setPattern(WhiteboardPattern pattern)
{
    // A colour picker drags through many of these; the look is already up.
    const bool changed = pattern != currentPattern.load();
    currentPattern = pattern;

    if (changed) switch (pattern) {
        case WhiteboardPattern::Noise:
            stateMachine->setNextState(noiseState);
            break;
        case WhiteboardPattern::Monocolor:
            stateMachine->setNextState(monoState);
            break;
        case WhiteboardPattern::RainbowChristmas:
            stateMachine->setNextState(rainbowChristmasState);
            break;
        case WhiteboardPattern::RainbowGradient:
            stateMachine->setNextState(rainbowGradientState);
            break;
        case WhiteboardPattern::Fire:
            stateMachine->setNextState(fireState);
            break;
        case WhiteboardPattern::MatrixRainbow:
            stateMachine->setNextState(matrixRainbowState);
            break;
        case WhiteboardPattern::MatrixColor:
            stateMachine->setNextState(matrixColorState);
            break;
        default:
            break;
    }

    dbgLog(("Pattern switched to: " + std::to_string(static_cast<int>(pattern))).c_str(), Verbosity::Display, Category::Relic);

    // Both entities show the pattern, so both hear about it, whichever asked.
    markModeChanged();
    markStateChanged();
}

void WhiteboardCore::tick(float deltaTime)
{
    RelicCore::tick(deltaTime);
    applyCommands();
    tickBrightness(deltaTime);
    stateMachine->tick(deltaTime);
}

void WhiteboardCore::tickWhileLinked(float deltaTime)
{
    applyCommands();
    tickBrightness(deltaTime);
}

void WhiteboardCore::tickNetwork()
{
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

    if (!connected)
    {
        return;
    }

    if (bHasHAConfig && !bDiscoveryPublished)
    {
        publishDiscovery();
    }

    const uint32_t state = stateRevision.load(std::memory_order_acquire);
    if (state != publishedStateRevision)
    {
        publishedStateRevision = state;
        publishState();
    }

    const uint32_t accent = accentRevision.load(std::memory_order_acquire);
    if (accent != publishedAccentRevision)
    {
        publishedAccentRevision = accent;
        publishAccentState();
    }

    const uint32_t mode = modeRevision.load(std::memory_order_acquire);
    if (mode != publishedModeRevision)
    {
        publishedModeRevision = mode;
        publishModeState();
    }
}

bool WhiteboardCore::handleCommand(string msg)
{
    if (strcmp(msg.c_str(), "next") == 0) {
        int nextPattern = (static_cast<int>(currentPattern.load()) + 1) % static_cast<int>(WhiteboardPattern::MAX);
        setPattern(static_cast<WhiteboardPattern>(nextPattern));
        return true;
    }

    return RelicCore::handleCommand(msg);
}
