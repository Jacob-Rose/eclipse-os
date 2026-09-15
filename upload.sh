#!/usr/bin/env bash
# Compile and upload in one go. ./upload.sh [obelisk|whiteboard] [deploy|dev] [-p PORT]
#
# One command rather than compile-then-upload because `arduino-cli upload` on
# its own sends whatever the sketch's build cache last held - which, now that
# the relic and the mode are build flags, may be a different sculpture's
# firmware than the one you just asked for. Without -p it takes the first
# board on the bus, same as monitor.sh.
set -u
export PATH="$HOME/.local/bin:$PATH"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$REPO/tools/firmware-env.sh" "$@"

firmware_check_secrets "$REPO"
firmware_find_port
firmware_describe
echo "port:   $PORT"
echo
cd "$REPO" && arduino-cli compile "${BUILD_ARGS[@]}" --upload -p "$PORT" --verbose eclipse-os.ino
