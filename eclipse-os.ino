// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <stdio.h>
#include <Arduino.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

namespace arduino = ::arduino;

#include <string>

#include "src/relics/obelisk.h"
#include "src/lib/ecore/logging.h"

using namespace std;
// Ensure the correct byte type is used
using byte = uint8_t;

unique_ptr<RelicCore> relic;

void setup() {
  Serial.begin(19200);
  
  delay(500);

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