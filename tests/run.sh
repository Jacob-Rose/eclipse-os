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
# The library (lib/src: ecore, eanim, esm, elink, edmx, emidi, eio minus pins) is compiled with USE_ARDUINO=0, the
# same switch the desktop build uses, so what runs here is the same code the
# relics run minus the pin-level IO. Needs g++ on PATH.

set -euo pipefail

cd "$(dirname "$0")/.."

OUT=build-tests
mkdir -p "$OUT"

CXX="${CXX:-g++}"
CXXFLAGS=(-std=c++20 -O1 -Wall -Wextra -DUSE_ARDUINO=0 -Ilib/src -Itests)

# the same portable slice desktop/CMakeLists.txt builds: everything but button.cpp
LIB_SOURCES=(lib/src/ecore/*.cpp lib/src/eanim/*.cpp lib/src/esm/*.cpp lib/src/elink/*.cpp lib/src/edmx/*.cpp lib/src/emidi/*.cpp
             lib/src/eio/hsv_strip.cpp lib/src/eio/relic.cpp lib/src/eio/screen_drawer.cpp lib/src/eio/strip_projection.cpp
             lib/src/external/easing.cpp)
TEST_SOURCES=(tests/*.cpp)

"$CXX" "${CXXFLAGS[@]}" "${LIB_SOURCES[@]}" "${TEST_SOURCES[@]}" -o "$OUT/eclipse-tests"

"$OUT/eclipse-tests" "$@"
