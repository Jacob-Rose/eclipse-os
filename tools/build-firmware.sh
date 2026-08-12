#!/usr/bin/env bash
#
# Builds eclipse-os.ino for the Pico W and leaves a .uf2 you can drag onto the
# board (or hand to tools/flash-firmware.sh).
#
#   tools/build-firmware.sh [output-dir]
#
# Prerequisites, once per machine: arduino-cli, the rp2040 core, the libraries,
# and tools/patch-libraries.sh. See the root readme.
#
# On Windows this wants to run inside WSL - the toolchain is a Linux one and the
# repo is reachable at /mnt/c/... from there.

set -u

export PATH="$HOME/.local/bin:$PATH"

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$REPO/build-firmware}"
FQBN="${FQBN:-rp2040:rp2040:rpipicow}"

if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "arduino-cli is not on PATH. See the root readme for the one-time setup." >&2
    exit 1
fi

echo "repo:  $REPO"
echo "fqbn:  $FQBN"
echo "out:   $OUT"
echo

rm -rf "$OUT"
mkdir -p "$OUT"

cd "$REPO" || exit 1
arduino-cli compile -b "$FQBN" --output-dir "$OUT" eclipse-os.ino
status=$?

if [ $status -ne 0 ]; then
    echo
    echo "Build failed. Two things account for most of it:" >&2
    echo "  * 'millis' not declared      -> run tools/patch-libraries.sh" >&2
    echo "  * 'byte'/'lerp' is ambiguous -> a header has opened std at global" >&2
    echo "                                  scope again; see src/lib/ecore/name.h" >&2
    exit $status
fi

echo
echo "uf2: $OUT/eclipse-os.ino.uf2"
