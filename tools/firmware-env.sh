#!/usr/bin/env bash
#
# The words every firmware script understands. Sourced, not run:
#
#   . "$REPO/tools/firmware-env.sh" "$@"
#
# and afterwards these are set:
#
#   FQBN          the board (rp2040:rp2040:rpipicow unless FQBN was already set)
#   RELIC         obelisk | whiteboard
#   DEPLOY        0 | 1
#   PORT          a serial port, or empty for "the first one plugged in"
#   BUILD_ARGS    an array of arduino-cli arguments that carry all of the above
#   REST          an array of whatever words were not ours (an output dir, say)
#
# The words, in any order:
#
#   obelisk | whiteboard    which sculpture. Default obelisk - the sketch's own
#                           default, so no words means what it always meant.
#   deploy | dev            production or development. Default dev.
#   -p PORT                 the port to upload to / monitor.
#
# Deploy is the build that lives on a wall for a month: DEPLOYMENT=1 for the
# sketch's tighter loop, and DEBUG_LOGGING_ENABLED=0 for *every* translation
# unit - the sketch's own define never reached the library, so a "deploy" build
# used to keep logging from everything under src/lib.
#
# These land in compiler.cpp.extra_flags rather than build.extra_flags: the
# Pico W board puts its CYW43 (WiFi) defines in the latter, and a
# --build-property replaces rather than appends.

FQBN="${FQBN:-rp2040:rp2040:rpipicow}"
RELIC="${RELIC:-obelisk}"
DEPLOY="${DEPLOY:-0}"
PORT="${PORT:-}"
REST=()

while [ $# -gt 0 ]; do
    case "$1" in
        obelisk|whiteboard) RELIC="$1" ;;
        deploy)             DEPLOY=1 ;;
        dev)                DEPLOY=0 ;;
        -p|--port)          shift; PORT="${1:-}" ;;
        -p*)                PORT="${1#-p}" ;;
        -h|--help)
            # the "The words" paragraphs above, the header's plumbing left out
            sed -n '/^# The words, in any order/,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)                  REST+=("$1") ;;
    esac
    shift
done

case "$RELIC" in
    obelisk)    RELIC_DEFINE=RELIC_OBELISK ;;
    whiteboard) RELIC_DEFINE=RELIC_WHITEBOARD ;;
    *)
        echo "unknown relic '$RELIC' (obelisk or whiteboard)" >&2
        exit 2
        ;;
esac

FIRMWARE_DEFINES="-DRELIC=$RELIC_DEFINE -DDEPLOYMENT=$DEPLOY"
if [ "$DEPLOY" = 1 ]; then
    FIRMWARE_DEFINES="$FIRMWARE_DEFINES -DDEBUG_LOGGING_ENABLED=0"
fi

BUILD_ARGS=(-b "$FQBN" --build-property "compiler.cpp.extra_flags=$FIRMWARE_DEFINES")

# The whiteboard cannot build without its credentials, and the compiler's way
# of saying so is a missing-header error four includes deep.
firmware_check_secrets() {
    if [ "$RELIC" = whiteboard ] && [ ! -f "$1/secrets.h" ]; then
        echo "the whiteboard needs $1/secrets.h - copy secrets.h.example and fill it in" >&2
        exit 1
    fi
}

# The first Pico on the bus, unless -p said otherwise. A running board is a
# serial port; a board already holding BOOTSEL is a UF2 drive, which the core
# lists as "UF2_Board uf2conv" - and uploads to directly, skipping the
# 1200-baud reset it would otherwise send down the serial port. Either will do
# for an upload; only a serial port will do for the monitor.
firmware_find_port() {
    if [ -n "$PORT" ]; then
        return 0
    fi
    local want="${1:-any}"
    local rows
    rows="$(arduino-cli board list 2>/dev/null)"
    PORT="$(printf '%s\n' "$rows" | grep -E '^(/dev/ttyACM|/dev/ttyUSB|/dev/cu\.usbmodem|COM)' | head -1 | awk '{print $1}')"
    if [ -z "$PORT" ] && [ "$want" = any ]; then
        PORT="$(printf '%s\n' "$rows" | awk '$2 == "uf2conv" {print $1; exit}')"
    fi
    if [ -z "$PORT" ]; then
        echo "no board found. plugged in? arduino-cli board list says:" >&2
        printf '%s\n' "$rows" >&2
        exit 1
    fi
}

# The relic's line from src/relics/wiring.h, as "GP5 x 300, NEO_BGR + NEO_KHZ800".
# Read off the source rather than known here, so there is still only one place
# a pin is written. Empty if the line is not in the shape wiring.h asks for.
firmware_wiring() {
    local name="k$(printf '%s' "${RELIC:0:1}" | tr '[:lower:]' '[:upper:]')${RELIC:1}"
    sed -n "s/.*StripWiring $name *{ *\([0-9]*\) *, *\([0-9]*\) *, *\([^}]*[^ }]\) *}.*/GP\1 x \2, \3/p" \
        "${REPO:-$(dirname "${BASH_SOURCE[0]}")/..}/src/relics/wiring.h"
}

firmware_describe() {
    echo "relic:  $RELIC"
    echo "wiring: $(firmware_wiring)"
    echo "mode:   $([ "$DEPLOY" = 1 ] && echo deploy || echo dev)"
    echo "fqbn:   $FQBN"
    echo "flags:  $FIRMWARE_DEFINES"
}
