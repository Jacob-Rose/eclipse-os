// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <string>

#define DEPLOYMENT 0 // 0 = dev, 1 = production. Disables usb debugging usually due to low delay between ticks
#define DEBUG_LOGGING_ENABLED 1 && !DEPLOYMENT // overwrites the one in logging.h
#define USE_LED_FOR_TICK 1 && !DEPLOYMENT
#define USE_SERIAL_INPUT 1 && !DEPLOYMENT

#include "src/lib/ecore/core.h"
#include "src/relics/obelisk.h"
#include "src/lib/ecore/logging.h"

using namespace std;
using namespace obelisk;
using namespace ecore;
using namespace ecore::log;

#define LED_PIN 25  // Onboard LED for RP2040

static unique_ptr<RelicCore> relic;

#if USE_LED_FOR_TICK
static bool bLEDOn{false};
#endif

void setup() {
  delay(300);

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

  relic = make_unique<ObeliskCore>();

  if(relic)
  {
    relic->init();
  }
}

void loop() {

  if(relic)
  {
    relic->runTick();
    delay(15);
#if !DEPLOYMENT
    delay(35);
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
    }
  }
#endif
}

void setup1() {

}

void loop1()
{

}

// not called anywhere, since when would it be? but worth including for knowledge
void cleanup(void)
{
  relic = nullptr;
}