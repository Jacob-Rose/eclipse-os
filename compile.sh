#!/usr/bin/env bash
# Compile the sketch. ./compile.sh [obelisk|whiteboard] [deploy|dev]
# The words are tools/firmware-env.sh's; no words is the obelisk, dev.
set -u
export PATH="$HOME/.local/bin:$PATH"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$REPO/tools/firmware-env.sh" "$@"

firmware_check_secrets "$REPO"
firmware_describe
echo
cd "$REPO" && arduino-cli compile "${BUILD_ARGS[@]}" eclipse-os.ino
