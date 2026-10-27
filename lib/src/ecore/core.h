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

#include <stdio.h>

#if USE_ARDUINO
#include <Arduino.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#else
// desktop / host build (see desktop/readme.md). the platform shim supplies the
// handful of Arduino symbols the portable library actually leans on.
#include <cstdint>
#include <cstddef>
#include "platform_host.h"
#endif