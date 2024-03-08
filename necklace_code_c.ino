// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include <stdio.h>
#include "pico/stdlib.h"

#include "necklace.h"
#include "io.h"
#include "gm.h"

Necklace necklace;

#define DEBUGGING_ENABLED 1

void setup() {
  Serial.begin(9600);

  GlobalManager::initSingleton();

  necklace.setup();

  Serial.println(F("Benchmark                Time (microseconds)"));

#if DEBUGGING_ENABLED
  delay(1000);
#endif
}

void loop(void) {
  necklace.loop();
}

void loop1(void)
{
  necklace.loop1();
}

// not used, since when would it be? but worth including for knowledge
void cleanup(void)
{
  GlobalManager::cleanupSingleton();
}