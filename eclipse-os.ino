// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <string>

#define DEPLOYMENT 0 // 0 = dev, 1 = production. Disables usb debugging usually due to low delay between ticks
#define DEBUG_LOGGING_ENABLED 1 && !DEPLOYMENT // overwrites the one in logging.h
#define USE_LED_FOR_TICK 1 && !DEPLOYMENT
#define USE_SERIAL_INPUT 1 && !DEPLOYMENT

#define USE_SERIAL_MQTT 0 // 0 = WiFi MQTT, 1 = Serial MQTT (via serial2mqtt gateway)

#include "src/lib/ecore/core.h"
#include "src/relics/todoist_whiteboard/todoist_whiteboard.h"
#include "src/lib/emqtt/mqtt_client.h"
#include "src/lib/ecore/logging.h"
#include "secrets.h"

#if USE_SERIAL_MQTT
  #include "src/lib/emqtt/mqtt_transport_serial.h"
#else
  #include "src/lib/ewifi/wifi_manager.h"
  #include "src/lib/emqtt/mqtt_transport_wifi.h"
#endif

using namespace ecore;
using namespace ecore::log;
using namespace emqtt;

#if !USE_SERIAL_MQTT
  using namespace ewifi;
#endif

#define LED_PIN 25  // Onboard LED for RP2040

#if !USE_SERIAL_MQTT
  static unique_ptr<WiFiManager> wifiManager;
#endif

static unique_ptr<MqttClient> mqttClient;
static unique_ptr<todoist_whiteboard::WhiteboardCore> relic;

#if USE_LED_FOR_TICK
static bool bLEDOn{false};
#endif

// Helper function to create MQTT configuration from secrets.h
MqttConfig createMqttConfig() {
  MqttConfig config(MQTT_SERVER, MQTT_CLIENT_ID);
  config.port = MQTT_PORT;
  config.username = MQTT_USERNAME;
  config.password = MQTT_PASSWORD;
  config.keepAlive = MQTT_KEEP_ALIVE;
  config.cleanSession = MQTT_CLEAN_SESSION;
  return config;
}

// Helper function to configure MQTT client settings
void configureMqttClient(MqttClient& client) {
  client.setAutoReconnect(MQTT_AUTO_RECONNECT);
  client.setReconnectInterval(MQTT_RECONNECT_INTERVAL);
#ifdef MQTT_MAX_RECONNECT_INTERVAL
  client.setMaxReconnectInterval(MQTT_MAX_RECONNECT_INTERVAL);
#endif
}

// Helper function to create Home Assistant configuration from secrets.h
HomeAssistantConfig createHomeAssistantConfig() {
  HomeAssistantConfig config;
  config.enabled = true;
  config.discoveryPrefix = HA_DISCOVERY_PREFIX;
  config.deviceName = HA_DEVICE_NAME;
  config.deviceId = HA_DEVICE_ID;
  config.manufacturer = HA_MANUFACTURER;
  config.model = HA_MODEL;
  config.swVersion = HA_SW_VERSION;
  config.hwVersion = HA_HW_VERSION;
  config.serialNumber = HA_SERIAL_NUMBER;
  config.configurationUrl = HA_CONFIG_URL;
  config.originName = HA_ORIGIN_NAME;
  config.originSwVersion = HA_ORIGIN_SW_VERSION;
  config.originSupportUrl = HA_ORIGIN_SUPPORT_URL;
  return config;
}

void setup() {
  delay(1000);

  Serial.begin(9600);
  
  // Wait for USB Serial to be ready (required for proper USB reset handling)
  // This allows arduino-cli to reset the device into bootloader mode automatically
  while (!Serial && millis() < 3000) {
    delay(10);
  }

#if USE_LED_FOR_TICK
  pinMode(LED_PIN, OUTPUT);
#endif

  delay(300);
  
#if DEBUG_LOGGING_ENABLED
  delay(4000); // wait for serial to be ready
  Serial.println("Eclipse OS v0.7.0");
  Serial.println("Copyright 2025 | Jake Rose");
  Serial.println("Initializing...\n");

  Serial.println("Debug logging is enabled... Expect performance impact.");
  Serial.println("Use #define DEBUG_LOGGING_ENABLED 0 to disable.\n");
#endif

#if USE_SERIAL_MQTT
  // Setup Serial MQTT transport
  Serial.println("Using Serial MQTT transport (via serial2mqtt gateway)");
  auto transport = make_unique<MqttTransportSerial>();
  mqttClient = make_unique<MqttClient>(std::move(transport));

  mqttClient->init(createMqttConfig());
  configureMqttClient(*mqttClient);
  mqttClient->connect();
#else
  // Initialize and connect WiFi
  wifiManager = make_unique<WiFiManager>();
  wifiManager->init(WIFI_SSID, WIFI_PASSWORD);
  wifiManager->setAutoReconnect(WIFI_AUTO_RECONNECT);
  wifiManager->setReconnectInterval(WIFI_RECONNECT_INTERVAL);
#ifdef WIFI_MAX_RECONNECT_INTERVAL
  wifiManager->setMaxReconnectInterval(WIFI_MAX_RECONNECT_INTERVAL);
#endif

  if (wifiManager->connect(WIFI_MAX_CONNECT_ATTEMPTS)) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(wifiManager->getLocalIP());
  } else {
    Serial.println("WiFi connection failed!");
  }

  // Setup WiFi MQTT transport
  auto transport = make_unique<MqttTransportWiFi>(wifiManager->getClient());
  mqttClient = make_unique<MqttClient>(std::move(transport));

  mqttClient->init(createMqttConfig());
  mqttClient->setWiFiConnectedCheck([&]() { return wifiManager && wifiManager->isConnected(); });
  configureMqttClient(*mqttClient);

  Serial.println("Connecting to MQTT broker...");
  if (mqttClient->connect()) {
    Serial.println("MQTT connected successfully!");
  } else {
    Serial.println("MQTT connection failed - discovery will retry on reconnect");
  }
#endif

  // Create relic with MQTT client
  relic = make_unique<todoist_whiteboard::WhiteboardCore>(*mqttClient);

  if(relic)
  {
    relic->init();
    // Set HA config - will publish discovery now or retry on reconnect
    relic->setHomeAssistantConfig(createHomeAssistantConfig());
  }
}

void loop() {

#if !USE_SERIAL_MQTT
  // Tick WiFi manager for auto-reconnect (only needed for WiFi mode)
  if(wifiManager)
  {
    wifiManager->tick(0.03f);  // ~30ms tick
  }
#endif

  // Tick MQTT client for connection management
  if(mqttClient)
  {
    mqttClient->tick(0.03f);  // ~30ms tick
  }

  if(relic)
  {
    relic->runTick();
    
#if !DEPLOYMENT
    delay(30);
#else
    auto tickStartTime = relic->getTickStartTime();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - tickStartTime;

    // convert to milliseconds:
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    float delayTime = std::max(20.0f - ms, 8.0f);
    if(delayTime > 0.0f)
    {
      delay(delayTime);
    }
#endif

#if USE_LED_FOR_TICK
    digitalWrite(LED_PIN, bLEDOn ? HIGH : LOW);
    bLEDOn = !bLEDOn; // toggle the LED every tick
#endif

#if USE_SERIAL_INPUT
    if(Serial.available())
    {
      string msg = Serial.readString().c_str();
      Serial.print("Received: ");
      Serial.println(msg.c_str());
      relic->handleCommand(msg);
    }
#endif
  }
}

void setup1() 
{
}

void loop1()
{
#if 0
  //Serial.println("Core 1 running...");
  if(relic.get())
  {
    relic->tick2();
  }

  delay(10);
#endif
}

// not called anywhere, since when would it be? but worth including for knowledge
void cleanup(void)
{
  relic = nullptr;
}