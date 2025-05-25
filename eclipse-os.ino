// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <string>

#define DEPLOYMENT 1 // 0 = dev, 1 = production. Disables usb debugging usually due to low delay between ticks
#define DEBUG_LOGGING_ENABLED 1 && !DEPLOYMENT // overwrites the one in logging.h
#define USE_LED_FOR_TICK 1 && !DEPLOYMENT
#define USE_SERIAL_INPUT 1 && !DEPLOYMENT

#include "src/lib/ecore/core.h"
#include "src/relics/obelisk/obelisk.h"
#include "src/lib/ecore/logging.h"

using namespace ecore;
using namespace ecore::log;

#define LED_PIN 25  // Onboard LED for RP2040

static unique_ptr<obelisk::ObeliskCore> relic;

#if USE_LED_FOR_TICK
static bool bLEDOn{false};
#endif

void setup() {
  delay(1000);

  Serial.begin(9600);

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

  relic = make_unique<obelisk::ObeliskCore>();

  if(relic)
  {
    relic->init();
  }
}

void loop() {

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