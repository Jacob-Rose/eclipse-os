#!/usr/bin/env bash
# Copyright 2024 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# Builds and runs the library tests on the host.
#
#   tests/run.sh            build and run everything in tests/
#   tests/run.sh hsv        run only the cases whose name contains "hsv"
#
# The portable library (ecore, eanim, esm, elink, eio minus pins) is compiled with USE_ARDUINO=0, the
# same switch the desktop build uses, so what runs here is the same code the
# relics run minus the pin-level IO. Needs g++ on PATH.

set -euo pipefail

cd "$(dirname "$0")/.."

OUT=build-tests
mkdir -p "$OUT"

CXX="${CXX:-g++}"
CXXFLAGS=(-std=c++17 -O1 -Wall -Wextra -DUSE_ARDUINO=0 -Isrc/lib -Itests)

# the same portable slice desktop/CMakeLists.txt builds: everything but button.cpp
LIB_SOURCES=(src/lib/ecore/*.cpp src/lib/eanim/*.cpp src/lib/esm/*.cpp src/lib/elink/*.cpp
             src/lib/eio/hsv_strip.cpp src/lib/eio/relic.cpp src/lib/eio/screen_drawer.cpp src/lib/eio/strip_projection.cpp
             src/lib/external/easing.cpp)
TEST_SOURCES=(tests/*.cpp)

"$CXX" "${CXXFLAGS[@]}" "${LIB_SOURCES[@]}" "${TEST_SOURCES[@]}" -o "$OUT/eclipse-tests"

"$OUT/eclipse-tests" "$@"
