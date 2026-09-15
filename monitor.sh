#!/usr/bin/env bash
# Serial monitor on the first board found, or -p PORT.
set -u
export PATH="$HOME/.local/bin:$PATH"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$REPO/tools/firmware-env.sh" "$@"

firmware_find_port serial
echo "Found device: $PORT"
echo "Starting monitor..."
echo
arduino-cli monitor -p "$PORT" --fqbn "$FQBN"
