#!/usr/bin/env bash
# Copyright 2024 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# The beep test for the mythos26 set: how late is the rig, and bake it in.
#
#   ./launch-mythos-set-latency.sh            the rig on the pi, the click here
#   ./launch-mythos-set-latency.sh --local    the wires on this machine
#   ./launch-mythos-set-latency.sh --bench    nothing on any wire (the screen only)
#
# Same road as launch-mythos-set.sh - the show renders here and the picture
# goes over ssh to the scanner-pi - so the number it measures is the number
# the set needs. It opens on the same config the set runs off, because that
# is the file `[s]` writes `midi.latency_ms` into. See "latency - the beep
# test" in readme.md for what to do once the window is up.

set -euo pipefail

cd "$(dirname "$0")"

CONFIG="${ECLIPSE_CONFIG:-config/mythos-show.json}"
PYTHON="${PYTHON:-python3}"
DEFAULT_HOST="scanner-pi"

# The far end that paints; see the same block in launch-mythos-set.sh. `-`
# rather than `:-` so an exported empty ECLIPSE_CLIENT means this machine's
# wires, the same as --local; unset means the pi.
CLIENT="${ECLIPSE_CLIENT-$DEFAULT_HOST}"
client_named=0
[ -n "${ECLIPSE_CLIENT+x}" ] && client_named=1

live=1
extra=()

usage() {
    cat <<'USAGE'
launch-mythos-set-latency.sh - the beep test, on this machine's speakers

  --client [NAME]   run the show HERE and flash NAME's wires (the default:
                    scanner-pi, or $ECLIPSE_CLIENT). Defaulted, a pi that does
                    not answer means this machine's wires; named, it means
                    the test does not start.
  --local           the wires are on this machine: no pi, no ssh
  --bench, --dry-run   nothing on any wire - the screen and the click only
  --no-click        no sound; the arrow keys and the flash only
  --bpm N           tempo of the test (default 120)
  --config PATH     the show to write the number into
                    (default config/mythos-show.json, or $ECLIPSE_CONFIG)
  --                everything after this is passed to eclipse_dmx as-is
  -h, --help        this

In the window: [up]/[down] move the latency 1ms (shift: 10ms) until the flash
lands on the click - or [b] and tap [space] to the click, [f] and tap to the
flash, then [a] applies what the taps say. [s] writes it into the config.
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --bench|--dry-run) live=0 ;;
        --no-click)        extra+=(--no-click) ;;
        --bpm)             extra+=(--bpm "${2:?--bpm needs a number}"); shift ;;
        --config)          CONFIG="${2:?--config needs a path}"; shift ;;
        --client)
            client_named=1
            if [ $# -ge 2 ] && [ -n "${2-}" ] && [ "${2#-}" = "$2" ]; then
                CLIENT="$2"; shift
            else
                CLIENT="$DEFAULT_HOST"
            fi
            ;;
        --local)           CLIENT=""; client_named=0 ;;
        --)                shift; extra+=("$@"); break ;;
        -h|--help)         usage; exit 0 ;;
        *)                 echo "unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
    shift
done

# ---- preflight ------------------------------------------------------------

if [ ! -f "$CONFIG" ]; then
    echo "no show at '$CONFIG'" >&2
    exit 1
fi

EXE=""
for candidate in "${ECLIPSE_DMX_BINARY:-}" build/eclipse-dmx build/Release/eclipse-dmx bin/eclipse-dmx; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
        EXE="$candidate"
        break
    fi
done
if [ -z "$EXE" ]; then
    echo "eclipse-dmx is not built. Run ./build.sh first." >&2
    exit 1
fi

if ! command -v "$PYTHON" >/dev/null 2>&1; then
    echo "'$PYTHON' is not on PATH. Set PYTHON=... to name your interpreter." >&2
    exit 1
fi

if ! "$PYTHON" -c "import tkinter" >/dev/null 2>&1; then
    echo "the beep test needs tkinter, which $PYTHON does not have." >&2
    echo "  debian/ubuntu: sudo apt install python3-tk    (arch: pacman -S tk)" >&2
    exit 1
fi

if [ -n "$CLIENT" ]; then
    # Batch mode, because a password prompt inside a pipe is a hang rather
    # than a question. A defaulted pi that does not answer is this machine's
    # wires with a warning; a named one is a refusal - the same rule as the
    # set's launcher.
    if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "$CLIENT" true 2>/dev/null; then
        if [ "$client_named" = 0 ]; then
            echo "warning: '$CLIENT' is not answering ssh - flashing this machine's" >&2
            echo "         wires instead. The obelisk and ring on the pi stay dark." >&2
            echo "         --client $CLIENT to insist; --local to stop asking." >&2
            CLIENT=""
        else
            echo "'ssh $CLIENT true' did not succeed - the test will not start." >&2
            echo "  ssh runs in batch mode here, so it needs a key; ~/.ssh/config's" >&2
            echo "  Host alias is what carries the user and the key." >&2
            exit 1
        fi
    fi
fi

if [ -n "$CLIENT" ]; then
    echo "client: $CLIENT - the show runs here, its wires flash there"
else
    echo "local: the wires are this machine's"
fi
[ "$live" = 0 ] && echo "bench: nothing is going to the wire."

# ---- the test -------------------------------------------------------------

command=("$PYTHON" -m eclipse_dmx calibrate "$CONFIG")
[ "$live" = 0 ] && command+=(--dry-run)
[ -n "$CLIENT" ] && command+=(--client "$CLIENT")
command+=("${extra[@]+"${extra[@]}"}")

echo
echo "listen to the click, watch the rig. [up]/[down] until they land together,"
echo "or [b] tap to the click, [f] tap to the flash, [a] apply. [s] writes it to"
echo "$CONFIG; every set from that file runs with it."
echo
echo "> ${command[*]}"
echo

PYTHONPATH="python${PYTHONPATH:+:$PYTHONPATH}" exec "${command[@]}"
