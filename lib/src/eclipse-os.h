// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

///
/// The library's front door. A sketch includes this once; the Arduino build
/// then knows the library is in use and puts lib/src on the include path, so
/// every module is reachable as <ecore/hsv.h>, <eanim/generator_hsv.h> and
/// so on. Include the modules you use by name; this only brings the core.
///

#include <ecore/core.h>
#include <ecore/logging.h>
