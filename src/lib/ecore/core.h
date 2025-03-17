// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#ifndef USE_ARDUINO
#define USE_ARDUINO 1
#endif

#ifndef USE_PICO_SDK
#define USE_PICO_SDK 0
#endif

#ifndef ERROR_CHECKING_ENABLED
#define ERROR_CHECKING_ENABLED 1
#endif

#ifndef DEBUG_LOGGING_ENABLED
#define DEBUG_LOGGING_ENABLED 1
#endif

#if USE_ARDUINO
#include <Arduino.h>
#endif

#if USE_PICO_SDK
#include <stdio.h>
#include "pico/stdlib.h"
#endif