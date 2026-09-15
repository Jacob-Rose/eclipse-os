#!/usr/bin/env bash
#
# Builds eclipse-os.ino for the Pico W and leaves a .uf2 you can drag onto the
# board.
#
#   tools/build-firmware.sh [obelisk|whiteboard] [deploy|dev] [output-dir]
#
# The words are tools/firmware-env.sh's. The .uf2 is named for what it is -
# eclipse-os.whiteboard.deploy.uf2 - so two builds in one folder cannot be
# mistaken for each other.
#
# Prerequisites, once per machine: arduino-cli, the rp2040 core, the libraries,
# and tools/patch-libraries.sh. See the root readme.
#
# On Windows this wants to run inside WSL - the toolchain is a Linux one and the
# repo is reachable at /mnt/c/... from there.

set -u

export PATH="$HOME/.local/bin:$PATH"

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/tools/firmware-env.sh" "$@"
OUT="${REST[0]:-$REPO/build-firmware}"
MODE="$([ "$DEPLOY" = 1 ] && echo deploy || echo dev)"

if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "arduino-cli is not on PATH. See the root readme for the one-time setup." >&2
    exit 1
fi

firmware_check_secrets "$REPO"

echo "repo:   $REPO"
firmware_describe
echo "out:    $OUT"
echo

mkdir -p "$OUT"

cd "$REPO" || exit 1
arduino-cli compile "${BUILD_ARGS[@]}" --output-dir "$OUT" eclipse-os.ino
status=$?

if [ $status -ne 0 ]; then
    echo
    echo "Build failed. Two things account for most of it:" >&2
    echo "  * 'millis' not declared      -> run tools/patch-libraries.sh" >&2
    echo "  * 'byte'/'lerp' is ambiguous -> a header has opened std at global" >&2
    echo "                                  scope again; see src/lib/ecore/name.h" >&2
    exit $status
fi

UF2="$OUT/eclipse-os.$RELIC.$MODE.uf2"
mv -f "$OUT/eclipse-os.ino.uf2" "$UF2"

echo
echo "uf2: $UF2"
