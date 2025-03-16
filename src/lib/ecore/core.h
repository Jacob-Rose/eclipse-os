// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#define USE_ARDUINO 1
#define USE_PICO_SDK 0

#define USE_ERROR_CHECKING 1

#if USE_ARDUINO
#include <Arduino.h>
#endif

#if USE_PICO_SDK
#include <stdio.h>
#include "pico/stdlib.h"
#endif