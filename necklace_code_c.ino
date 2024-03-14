// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "src/necklace.h"
#include "src/gm.h"
#include "src/logging.h"

#include <string>

Necklace necklace;

void setup() {
  Serial.begin(19200);

  GlobalManager::initSingleton();
  SPI.begin();

  delay(3000);

  necklace.setup();
}

void loop() {
  necklace.loop();
}

void setup1() {
  delay(1000);
}

void loop1()
{
  necklace.loop1();
}

// not called anywhere, since when would it be? but worth including for knowledge
void cleanup(void)
{
  GlobalManager::cleanupSingleton();
}