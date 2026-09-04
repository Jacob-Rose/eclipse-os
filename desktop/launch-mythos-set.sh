#!/usr/bin/env bash
# Copyright 2024 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# Launches the mythos26 set on Linux: the show, the viewer, the wire, the
# colour out to Synesthesia and its audio analysis back in - one process, off
# config/mythos26.json.
#
#   ./launch-mythos-set.sh              the set: live rig, viewer, OSC
#   ./launch-mythos-set.sh --bench      the same look, nothing on the wire
#   ./launch-mythos-set.sh --headless   no window (over ssh, or no tkinter)
#
# The Linux counterpart of start.bat at the top of the checkout, with the
# preflight that machine needs and Windows does not.

set -euo pipefail

cd "$(dirname "$0")"

CONFIG="config/mythos26.json"
OSC_DEVICE="synesthesia"        # devices/synesthesia.json: the room's colour
PYTHON="${PYTHON:-python3}"

# Where the visualiser is: the mac mini, which is the machine Synesthesia runs
# on. Named rather than addressed on purpose - this network hands out addresses
# by DHCP, the name follows the machine across a new lease and an address does
# not, and the sender looks the name up again every 15s while the set runs.
#
# It answers to "Jakes-Mac-mini.local", not "mac-mini.local" - that is the name
# macOS built from the computer's name in Sharing, and it is what it announces
# on mDNS. On 2026-09-03 that resolved to 192.168.50.216, which is the number to
# put here if a venue's network has no mDNS to ask (see readme.md, "naming the
# machine instead of its address").
#
# ECLIPSE_OSC_ADDRESS overrides it for a machine that is not this rig, and --osc
# overrides both.
OSC_ADDRESS="${ECLIPSE_OSC_ADDRESS:-Jakes-Mac-mini.local:6000}"

# The way back: Synesthesia's audio engine, driving this rig's knobs. Its FFT
# is the only thing in the room doing real analysis - the beat off Mixxx is a
# grid and a VU meter and nothing else - so bass, highs and presence come back
# in on this port and land on whatever config/oscmaps/synesthesia.json binds
# them to. Turn it on in the app: Settings > OSC > Output Audio Variables, with
# the output port set to this number and the output IP naming this machine.
OSC_IN_PORT="${ECLIPSE_OSC_IN_PORT:-7000}"

# The controller, for the cue pads and the midi map's learn button.
#
# The config says "auto", and on Linux auto will not open a controller at all:
# it publishes "eclipse-dmx IN" for Mixxx and waits, because every pad on a box
# of buttons sends notes and one of them landing on beat_note would yank the
# beat grid. So the Launchpad has to be named, and naming it does not cost the
# tempo route - the published port stays up alongside the subscription, so
# Mixxx still reaches us on the same run.
#
# Named, not addressed: this is "20:1" to aconnect today and something else
# after a replug, the same reason the mac mini is a name up there. The X shows
# up as two ports and only one of them carries your pads - the MIDI port for
# the standalone custom and note modes, the DAW port for a session-mode host.
# Which one is a question for the controller, not for this file:
#
#   python -m eclipse_dmx midi-watch config/mythos26.json \
#       --midi "Launchpad X LPX DAW In" --seconds 15
#
# and press some pads. Whichever prints is the one to put here.
#
# Worth knowing once they are in: the pads share the input with the beat clock,
# so a pad sending note 50, 52, 64, 68 or 69 on channel 1 reads as beat, tempo
# or VU. Move the controller's custom mode to another channel if they collide.
MIDI_PORT="${ECLIPSE_MIDI_PORT:-Launchpad X LPX MIDI In}"

# The other direction down the same cable: the pads, lit from the map.
#
# A different port from the one above, and not optional to get right - "MIDI
# In" is what the Launchpad listens on, "MIDI Out" is what it sends. Naming
# the input here would open a port that never lights anything.
#
# Worth knowing before it is switched on: lighting a Launchpad X means putting
# it into Programmer mode, because Live mode does not accept host LED messages
# at all (see the readme, and eclipse_dmx/launchpad.py). Programmer mode also
# changes what the pads *send* - the grid becomes notes 11-88 - so bindings
# learned in a custom mode will need learning again. Empty string lights
# nothing and leaves the controller alone.
MIDI_OUT="${ECLIPSE_MIDI_OUT:-Launchpad X LPX MIDI Out}"

live=1
viewer=1
osc=1
osc_in=1
extra=()
midi_args=()

usage() {
    cat <<'USAGE'
launch-mythos-set.sh - the mythos26 set, on this machine

  --bench, --dry-run   render and draw, but put nothing on any wire
  --headless           run without the viewer window (ssh, or no tkinter)
  --no-osc             do not send the rig's colour to a visualiser
  --no-osc-in          do not take the visualiser's audio analysis back in
  --osc-in PORT        listen for it on this port instead (default 7000, or
                       $ECLIPSE_OSC_IN_PORT). Must match Synesthesia's OSC
                       *output* port; `osc-watch` shows what arrives.
  --osc HOST:PORT      where the visualiser is listening. Defaults to the mac
                       mini, Jakes-Mac-mini.local:6000, or $ECLIPSE_OSC_ADDRESS.
                       A name is followed if its address changes.
  --midi SPEC          controller to take pads from (default the Launchpad,
                       or $ECLIPSE_MIDI_PORT). Empty string opens none and
                       leaves the config's "auto" to publish for Mixxx.
  --midi-out SPEC      controller to light from the map (default the
                       Launchpad, or $ECLIPSE_MIDI_OUT). Empty string lights
                       nothing. Puts the pad grid into Programmer mode.
  --config PATH        a different show (default config/mythos26.json)
  --bpm N              opening tempo, and the fallback if the beat goes quiet
  --state NAME         the look to open on (beat_pulse, vu_pulse, tv_static...)
  --                   everything after this is passed to eclipse_dmx as-is
  -h, --help           this

Ctrl-C ends the set; the rig is put dark on the way out.
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --bench|--dry-run) live=0 ;;
        --headless)        viewer=0 ;;
        --no-osc)          osc=0 ;;
        --no-osc-in)       osc_in=0 ;;
        --osc-in)          OSC_IN_PORT="${2:?--osc-in needs a port}"; shift ;;
        --osc)             OSC_ADDRESS="${2:?--osc needs HOST:PORT}"; shift ;;
        --config)          CONFIG="${2:?--config needs a path}"; shift ;;
        --midi)            MIDI_PORT="${2-}"; shift ;;
        --midi-out)        MIDI_OUT="${2-}"; shift ;;
        --bpm)             extra+=(--bpm "${2:?--bpm needs a number}"); shift ;;
        --state)           extra+=(--state "${2:?--state needs a name}"); shift ;;
        --)                shift; extra+=("$@"); break ;;
        -h|--help)         usage; exit 0 ;;
        *)                 echo "unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
    shift
done

# ---- preflight ------------------------------------------------------------
# All of it before the first frame, because the failures below look identical
# once a window is open and the rig is dark.

if [ ! -f "$CONFIG" ]; then
    echo "no show at '$CONFIG'" >&2
    exit 1
fi

# Same order binary.py looks in, so the script and the wrapper never disagree
# about which build is running.
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

if [ "$viewer" = 1 ] && ! "$PYTHON" -c "import tkinter" >/dev/null 2>&1; then
    echo "the viewer needs tkinter, which $PYTHON does not have." >&2
    echo "  debian/ubuntu: sudo apt install python3-tk    (arch: pacman -S tk)" >&2
    echo "  or run the set with --headless" >&2
    exit 1
fi

if [ "$live" = 1 ]; then
    # A device with no wire does not stop the show - it comes up OFFLINE and
    # keeps rendering - so none of this is fatal. It is said now because at
    # the venue the difference between "not plugged in" and "held by something
    # else" is worth knowing before the music starts.
    ports="$("$EXE" --list-ports 2>/dev/null | sed -n 's/^PORT /  /p')"
    if [ -n "$ports" ]; then
        echo "ports:"
        echo "$ports"
    else
        echo "no serial ports visible - is the widget plugged in?" >&2
    fi

    # The truss's Open DMX widget goes through libftdi rather than the tty on
    # Linux (see readme.md), and that path has no permissions problem. Only the
    # serial fallback does, so only warn about a node we cannot actually open.
    for node in /dev/ttyUSB* /dev/ttyACM*; do
        if [ -e "$node" ] && { [ ! -r "$node" ] || [ ! -w "$node" ]; }; then
            group="$(stat -c '%G' "$node" 2>/dev/null || echo '?')"
            echo "warning: $node is group '$group' and this user cannot open it." >&2
            echo "         sudo usermod -aG $group $USER, then log out and back in." >&2
        fi
    done

    # One process owns a widget. QLC+ takes its output at startup and holds it,
    # and the only symptom downstream is a refusal to open the port.
    if pgrep -x qlcplus >/dev/null 2>&1; then
        echo "warning: QLC+ is running and may already hold the widget. Close it." >&2
    fi
fi

# What the machine can hear and light, read once and held.
#
# Held rather than piped straight into grep, and that is not tidiness: this
# script runs under `set -o pipefail`, and `grep -q` exits the moment it
# matches, which hands the still-writing executable a SIGPIPE and poisons the
# pipeline's status. The test then fails *because* the port was found, as long
# as it was found before the last line - which is why naming the input port
# silently did nothing while the output port, last in the list, worked.
midi_ports=""
if [ -n "$MIDI_PORT" ] || [ -n "$MIDI_OUT" ]; then
    midi_ports="$("$EXE" --list-midi 2>/dev/null || true)"
fi

has_midi_port() {
    [ -n "$midi_ports" ] && printf '%s\n' "$midi_ports" | grep -Fq "$1"
}

if [ -n "$MIDI_PORT" ]; then
    # Only if it is actually plugged in. A *named* port that is not there is
    # fatal at startup - deliberately, since naming one means you meant it -
    # and a controller left in the flight case must not be the reason a set
    # does not run. Unplugged, the config's "auto" publishes for Mixxx exactly
    # as it did before, and the cue pads are simply not there tonight.
    if has_midi_port "$MIDI_PORT"; then
        # Into its own array rather than `extra`, so it goes on the command
        # line *before* anything passed after `--` - argparse takes the last
        # of a repeated option, and a --midi typed at the prompt has to beat
        # the one this file defaults to.
        midi_args=(--midi "$MIDI_PORT")
        echo "controller: $MIDI_PORT"
    else
        echo "warning: no '$MIDI_PORT' on this machine - no cue pads tonight." >&2
        echo "         the beat still arrives on 'eclipse-dmx IN'." >&2
        echo "         --list-midi shows what is here." >&2
    fi
fi

# Lamps are the viewer's job: they are painted from the midi map, and `run`
# has no map to paint from. Headless is a set with no window and no pads lit,
# which is what it already was.
if [ -n "$MIDI_OUT" ] && [ "$viewer" = 1 ]; then
    # Same rule as the input: only when it is actually here. Never fatal in
    # the viewer either - a lamp that cannot be lit is a set without lamps -
    # but checking here means the reason is printed next to the rest of the
    # load-in rather than said once in a status line nobody was watching.
    if has_midi_port "$MIDI_OUT"; then
        midi_args+=(--midi-out "$MIDI_OUT")
        echo "lamps: $MIDI_OUT"
    else
        echo "warning: no '$MIDI_OUT' on this machine - the pads stay dark." >&2
        echo "         everything else runs; only the picture of the map is gone." >&2
    fi
fi

if [ "$osc" = 1 ]; then
    # A quick look at where the visualiser is, through the same code the sender
    # uses, so a name prints as an address before anything opens. Three seconds
    # because that is the shape of the answer: an mDNS hit comes back in
    # milliseconds and a miss takes seven seconds to give up, and waiting out
    # the miss buys nothing - the link looks the name up again on its own while
    # the set runs, and explains the failure properly when it does.
    resolved=0
    PYTHONPATH="python${PYTHONPATH:+:$PYTHONPATH}" timeout 3 "$PYTHON" - "$OSC_ADDRESS" <<'PREFLIGHT' || resolved=$?
import sys
from eclipse_dmx import osc

try:
    host, port = osc.parse_endpoint(sys.argv[1])
except ValueError as error:
    print(f"error: {error}", file=sys.stderr)
    sys.exit(2)                     # a typo, and the show would refuse it too

if osc.is_literal(host):
    sys.exit(0)                     # an address is where it says it is
try:
    _family, sockaddr = osc.resolve(host, port)
except OSError:
    sys.exit(1)                     # not up yet; the sender says why, in a moment
print(f"visualiser: {host} is {sockaddr[0]}")
PREFLIGHT

    if [ "$resolved" = 2 ]; then
        exit 2
    elif [ "$resolved" != 0 ]; then
        echo "warning: '$OSC_ADDRESS' does not resolve yet - starting anyway," >&2
        echo "         and following the name if it turns up." >&2
    fi
fi

# ---- the set --------------------------------------------------------------

command=("$PYTHON" -m eclipse_dmx)

if [ "$viewer" = 1 ]; then
    command+=(view "$CONFIG")
    [ "$live" = 1 ] && command+=(--live)
    [ "$osc" = 1 ] && command+=(--osc "$OSC_ADDRESS" --osc-device "$OSC_DEVICE")
elif [ "$osc" = 1 ]; then
    # The osc subcommand is the same show without a window; it drives the rig
    # unless told not to, which is the opposite of the viewer's default.
    command+=(osc "$CONFIG" --device "$OSC_DEVICE" --address "$OSC_ADDRESS")
    [ "$live" = 0 ] && command+=(--dry-run)
else
    command+=(run "$CONFIG")
    [ "$live" = 0 ] && command+=(--dry-run)
fi

if [ "$osc_in" = 1 ]; then
    # The map beside the config is found by name; naming a port here is what
    # turns the listener on. A port already held is a warning, not a refusal -
    # see _osc_input_for.
    command+=(--osc-in "$OSC_IN_PORT")
fi

command+=("${midi_args[@]+"${midi_args[@]}"}")
command+=("${extra[@]+"${extra[@]}"}")

# Mixxx enumerates MIDI once, at startup, and this is the port it connects to -
# so the order is not a preference. Said every launch because it is the failure
# that wastes a set: everything looks right and no beat ever arrives.
echo
echo "start this before Mixxx. In Preferences > Controllers pick 'eclipse-dmx IN',"
echo "load 'MIDI for light', Apply. If Mixxx is already open, restart it."
[ "$live" = 0 ] && echo "bench: nothing is going to the wire."
echo
echo "> ${command[*]}"
echo

PYTHONPATH="python${PYTHONPATH:+:$PYTHONPATH}" exec "${command[@]}"
