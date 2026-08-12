// Copyright 2024 | Jake Rose
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <string>

// ---------------------------------------------------------------------------
// Which relic is this board?
//
// One line, because a Pico is flashed for exactly one sculpture and everything
// below follows from it: the obelisk wants no network at all, the whiteboard
// cannot work without one.
// ---------------------------------------------------------------------------
#define RELIC_OBELISK   1
#define RELIC_WHITEBOARD 2

#define RELIC RELIC_OBELISK

#define DEPLOYMENT 0 // 0 = dev, 1 = production. Disables usb debugging usually due to low delay between ticks
#define DEBUG_LOGGING_ENABLED 1 && !DEPLOYMENT // overwrites the one in logging.h
#define USE_LED_FOR_TICK 1 && !DEPLOYMENT

// The desk's cable. Deliberately *not* gated on DEPLOYMENT: the build that
// goes on a sculpture for a show is precisely the build a show needs to reach,
// and the old USE_SERIAL_INPUT being dev-only meant the deployed firmware was
// the one with no way in.
//
// It replaces the old raw Serial.readString() path rather than sitting beside
// it - two readers on one port eat each other's bytes. Typing into a serial
// monitor still works: RelicLink gathers printable bytes seen between frames
// into a line and hands it to handleCommand, same as before.
#define USE_RELIC_LINK 1

// Only the whiteboard talks to Home Assistant. The obelisk has no network and
// needs no secrets.h to build.
#if RELIC == RELIC_WHITEBOARD
  #define USE_MQTT 1
  #define USE_SERIAL_MQTT 0 // 0 = WiFi MQTT, 1 = Serial MQTT (via serial2mqtt gateway)
#else
  #define USE_MQTT 0
  #define USE_SERIAL_MQTT 0
#endif

#if USE_RELIC_LINK && USE_SERIAL_MQTT
  #error "The link and the serial MQTT bridge cannot share one port. Pick one."
#endif

#include "src/lib/ecore/core.h"
#include "src/lib/ecore/logging.h"

#if USE_RELIC_LINK
  #include "src/lib/elink/serial_transport.h"
#endif

#if RELIC == RELIC_OBELISK
  #include "src/relics/obelisk/obelisk.h"
#else
  #include "src/relics/todoist_whiteboard/todoist_whiteboard.h"
#endif

#if USE_MQTT
  #include "src/lib/emqtt/mqtt_client.h"
  #include "secrets.h"

  #if USE_SERIAL_MQTT
    #include "src/lib/emqtt/mqtt_transport_serial.h"
  #else
    #include "src/lib/ewifi/wifi_manager.h"
    #include "src/lib/emqtt/mqtt_transport_wifi.h"
  #endif
#endif

using namespace ecore;
using namespace ecore::log;

#if USE_MQTT
  using namespace emqtt;
  #if !USE_SERIAL_MQTT
    using namespace ewifi;
  #endif
#endif

#define LED_PIN 25  // Onboard LED for RP2040

#if USE_MQTT && !USE_SERIAL_MQTT
  static unique_ptr<WiFiManager> wifiManager;
#endif

#if USE_MQTT
  static unique_ptr<MqttClient> mqttClient;
#endif

#if RELIC == RELIC_OBELISK
  static unique_ptr<obelisk::ObeliskCore> relic;
  static const char* RELIC_NAME = "obelisk";
#else
  static unique_ptr<todoist_whiteboard::WhiteboardCore> relic;
  static const char* RELIC_NAME = "todoist_whiteboard";
#endif

#if USE_RELIC_LINK
static elink::SerialLinkTransport linkTransport;
#endif

#if USE_LED_FOR_TICK
static bool bLEDOn{false};
#endif

#if USE_MQTT
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
#endif

void setup() {
  delay(1000);

  // On an RP2040 Serial is USB CDC and this number is decoration: the host sets
  // a baud rate, neither end obeys it, and throughput is USB's. It matters on a
  // real UART and nowhere else.
  Serial.begin(115200);

#if USE_LED_FOR_TICK
  pinMode(LED_PIN, OUTPUT);
#endif

  delay(300);

#if DEBUG_LOGGING_ENABLED
  delay(4000); // wait for serial to be ready
  Serial.println("Eclipse OS v0.7.0");
  Serial.println("Copyright 2025 | Jake Rose");
  Serial.print("Relic: ");
  Serial.println(RELIC_NAME);
  Serial.println("Initializing...\n");

  Serial.println("Debug logging is enabled... Expect performance impact.");
  Serial.println("Use #define DEBUG_LOGGING_ENABLED 0 to disable.\n");
#endif

#if USE_MQTT
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
  configureMqttClient(*mqttClient);

  Serial.println("Connecting to MQTT broker...");
  if (mqttClient->connect()) {
    Serial.println("MQTT connected successfully!");
  } else {
    Serial.println("MQTT connection failed - discovery will retry on reconnect");
  }
#endif
#endif

  // Create the relic
#if RELIC == RELIC_OBELISK
  relic = make_unique<obelisk::ObeliskCore>();
#else
  relic = make_unique<todoist_whiteboard::WhiteboardCore>(*mqttClient);
#endif

  if(relic)
  {
    relic->init();

#if USE_RELIC_LINK
    // Live from here on. Until a desk sends something the relic runs its own
    // patterns exactly as it always has; the link costs a branch per tick.
    relic->getLink().setTransport(&linkTransport);
    relic->getLink().setIdentity(RELIC_NAME);
#endif

#if USE_MQTT
    // Set HA config - will publish discovery now or retry on reconnect
    relic->setHomeAssistantConfig(createHomeAssistantConfig());
#endif
  }
}

void loop() {

#if USE_MQTT && !USE_SERIAL_MQTT
  // Tick WiFi manager for auto-reconnect (only needed for WiFi mode)
  if(wifiManager)
  {
    wifiManager->tick(0.03f);  // ~30ms tick
  }
#endif

#if USE_MQTT
  // Tick MQTT client for connection management
  if(mqttClient)
  {
    mqttClient->tick(0.03f);  // ~30ms tick
  }
#endif

  if(relic)
  {
    // runTick reads the link, applies whatever the desk sent, and skips the
    // relic's own rendering while the desk owns the pixels. See
    // RelicCore::runTick.
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
