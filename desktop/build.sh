#!/usr/bin/env bash
# Copyright 2024 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# Builds eclipse-dmx on Linux/macOS. Binary lands in build/eclipse-dmx.

set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build --config "${BUILD_TYPE}" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo
echo "built: $(pwd)/build/eclipse-dmx"
echo "try:   ./build/eclipse-dmx --config config/example.json --dry-run --frames 5"
