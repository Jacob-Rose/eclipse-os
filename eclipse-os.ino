// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <string>

#include "src/lib/ecore/core.h"
#include "src/relics/obelisk.h"
#include "src/lib/ecore/logging.h"

using namespace std;
using namespace obelisk;
using namespace ecore;
using namespace ecore::log;

#define LED_PIN 25  // Onboard LED for RP2040

static unique_ptr<RelicCore> relic;

static bool bLEDOn{false};

void setup() {
  delay(300);

  Serial.begin(9600);

  pinMode(LED_PIN, OUTPUT);
  
  delay(400); // wait for serial to be ready
  Serial.println("Eclipse OS v0.1.0");
  Serial.println("Copyright 2024 | Jake Rose\n");
  Serial.println("Initializing...");

  relic = make_unique<ObeliskCore>();

  dbgLog("setup", Verbosity::Display, Category::StateInfo);

  if(relic)
  {
    relic->init();
  }
}

void loop() {

  if(relic)
  {
    relic->runTick();
    delay(40);

    digitalWrite(LED_PIN, bLEDOn ? HIGH : LOW);
    bLEDOn = !bLEDOn; // toggle the LED every tick
  }
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