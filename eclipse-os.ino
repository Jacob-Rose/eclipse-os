// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <stdio.h>
#include <Arduino.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "src/relics/obelisk.h"
#include "src/lib/ecore/logging.h"

#include <string>

Obelisk obelisk;

void setup() {
  Serial.begin(19200);
  
  delay(500);

  obelisk.init();
}

void loop() {
  obelisk.loop();
}

void setup1() {
  //delay(500);
}

void loop1()
{
  //obelisk.loop1();
  //delay(10);
}

// not called anywhere, since when would it be? but worth including for knowledge
void cleanup(void)
{
}