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

unique_ptr<RelicCore> relic;

void setup() {
  Serial.begin(4800);
  
  delay(400);

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
    delay(40);
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