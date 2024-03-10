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

#define DEBUGGING_ENABLED 1

void setup() {
  Serial.begin(9600);

  delay(1000);

  GlobalManager::initSingleton();

  necklace.setup();

  jlog::print(("SETUP COMPLETE"));

#if DEBUGGING_ENABLED
  delay(1000);
#endif
}

void setup1() {
  necklace.setup1();

  jlog::print(("SETUP1 COMPLETE"));
}

void loop(void) {
  necklace.loop();

  jlog::print(("LOOP0 COMPLETE"), Verbosity::Verbose);
}

void loop1(void)
{
  necklace.loop1();

  jlog::print(("LOOP1 COMPLETE"), Verbosity::Verbose);
}

// not used, since when would it be? but worth including for knowledge
void cleanup(void)
{
  GlobalManager::cleanupSingleton();
}