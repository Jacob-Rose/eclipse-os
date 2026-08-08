#!/usr/bin/env bash
# Copyright 2024 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# Builds eclipse-dmx on Linux/macOS.
#
#   ./tools/setup-toolchain.sh    (once per machine)
#   ./build.sh
#
# Uses the toolchain recorded by setup-toolchain.sh when there is one.

set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE="${BUILD_TYPE:-Release}"

# ---- toolchain ------------------------------------------------------------
if [ -f tools/toolchain.env ]; then
    # shellcheck disable=SC1091
    . tools/toolchain.env
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is not available. Run ./tools/setup-toolchain.sh first." >&2
    exit 1
fi

CONFIGURE_ARGS=(-S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}")
if [ -n "${ECLIPSE_DMX_CXX:-}" ]; then
    CONFIGURE_ARGS+=(-DCMAKE_CXX_COMPILER="${ECLIPSE_DMX_CXX}")
fi

if [ "${1:-}" = "--clean" ]; then
    echo "removing build/"
    rm -rf build
fi

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "configuring (${BUILD_TYPE})..."
cmake "${CONFIGURE_ARGS[@]}"

echo "building..."
cmake --build build --config "${BUILD_TYPE}" -j "${JOBS}"

echo
echo "built: $(pwd)/build/eclipse-dmx"
echo "try:   ./build/eclipse-dmx --config config/example.json --dry-run --frames 5"
