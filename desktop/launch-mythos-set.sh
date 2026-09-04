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

# The way back: Synesthesia's audio engine, which is the source for this show.
# Its FFT is the only thing in the room doing real analysis, so the levels, the
# transients, the presence *and* the beat come back in on this port and land on
# the channels config/oscmaps/synesthesia.json binds them to - see "audio" in
# config/mythos26.json. Turn it on in the app: Settings > OSC > Output Audio
# Variables, with the output port set to this number and the output IP naming
# this machine.
#
# --no-osc-in is the fallback if it will not start: the beat goes back to
# Mixxx's grid on the MIDI cable, the additive hit layers go dark, and every
# cue runs as it did.
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

# Where the show runs. Empty is this machine.
#
# With a host, the executable runs there over ssh and the window stays here -
# the control protocol is lines on two pipes and `ssh host cmd` is exactly two
# pipes, so nothing above ShowController notices. The point is where the wires
# are: the obelisk's USB cable and the scanner's ring are on the pi, so the
# frames never leave it.
#
# What does *not* cross the link is MIDI. The executable reads it, so a
# controller plugged in here is invisible to a show on the pi - see the block
# below, which is why the pads and the lamps default to off when a host is
# named. The OSC sender is desk-side off the frame stream and is unaffected.
HOST="${ECLIPSE_HOST:-}"

# Client mode: the show runs *here* and the far end only paints. The opposite
# trade from --host, and the reason to want it is MIDI - the executable is what
# opens a port, so under --host the beat from Mixxx and every pad on the
# Launchpad belong to the pi and this desk has neither. Under --client they
# stay here, where they are plugged in, and the frames go over instead.
#
# The cost is the other way round: the rig's picture now arrives over the
# network, so a hiccup is visible on the sculpture rather than only in the
# preview. Under --host the far end renders locally and a stall costs you
# nothing but a stuttery window.
CLIENT="${ECLIPSE_CLIENT:-}"

# The ssh *alias*, not the hostname - `scanner-pi`, not `scanner-pi.local`.
# ~/.ssh/config is where the user and the key live:
#
#     Host scanner-pi
#         HostName scanner-pi.local
#         User jakee
#         IdentityFile ~/.ssh/scanner_pi
#
# Naming the machine directly walks past all of that and ssh falls back to
# default identities, which on a machine whose only key is named for this host
# means none - "Permission denied (publickey)" for a key that is sitting right
# there. Anything with a Host block wants the block's name.
DEFAULT_HOST="scanner-pi"

live=1
viewer=1
osc=1
osc_in=1
extra=()
midi_args=()

# Whether the MIDI ports were *asked for* rather than defaulted. Naming one
# means you meant it, and over ssh that is the difference between "light the
# Launchpad on the pi" and "the author of this file guessed".
midi_named=0
midi_out_named=0
[ -n "${ECLIPSE_MIDI_PORT:-}" ] && midi_named=1
[ -n "${ECLIPSE_MIDI_OUT:-}" ] && midi_out_named=1

usage() {
    cat <<'USAGE'
launch-mythos-set.sh - the mythos26 set, on this machine

  --bench, --dry-run   render and draw, but put nothing on any wire
  --headless           run without the viewer window (ssh, or no tkinter)
  --no-osc             do not send the rig's colour to a visualiser
  --no-osc-in          do not take the visualiser's audio analysis back in,
                       whatever the config declares. The hit layers go dark;
                       everything else runs as it does.
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
  --host [NAME]        run the show on another machine over ssh, keeping the
                       window here (default scanner-pi.local, or $ECLIPSE_HOST).
                       The wires - DMX widget, obelisk, ring - are the host's.
                       So is MIDI: the pads and lamps default to off unless
                       --midi/--midi-out are named, because the controller
                       plugged in here is invisible to a show over there.
  --client [NAME]      run the show HERE and put the frames on NAME's wires
                       (default scanner-pi, or $ECLIPSE_CLIENT). The opposite
                       of --host: Mixxx and the Launchpad keep working,
                       because they never leave this machine. The rig's
                       picture crosses the network instead of its cues.
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
        --midi)            MIDI_PORT="${2-}"; midi_named=1; shift ;;
        --midi-out)        MIDI_OUT="${2-}"; midi_out_named=1; shift ;;
        --host)
            # The name is optional: bare --host means the usual one.
            if [ $# -ge 2 ] && [ -n "${2-}" ] && [ "${2#-}" = "$2" ]; then
                HOST="$2"; shift
            else
                HOST="$DEFAULT_HOST"
            fi
            ;;
        --client)
            if [ $# -ge 2 ] && [ -n "${2-}" ] && [ "${2#-}" = "$2" ]; then
                CLIENT="$2"; shift
            else
                CLIENT="$DEFAULT_HOST"
            fi
            ;;
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

if [ -z "$EXE" ] && [ -z "$HOST" ]; then
    echo "eclipse-dmx is not built. Run ./build.sh first." >&2
    exit 1
fi

if [ -n "$HOST" ] && [ -n "$CLIENT" ]; then
    echo "--host and --client are opposite ways round; pick one." >&2
    echo "  --host   the show runs there, this is a window onto it" >&2
    echo "  --client the show runs here, that machine only paints" >&2
    exit 1
fi

# Whichever way round it is, one machine is at the far end of an ssh link.
REMOTE="${HOST:-$CLIENT}"

if [ -n "$CLIENT" ]; then
    echo "client: $CLIENT - the show runs here, its wires are painted there"
    echo "        pads, lamps and the beat stay on this machine"
fi

if [ -n "$HOST" ]; then
    # ShowController runs ssh instead of the binary, so there is nothing local
    # to be missing - the host's build is the one that matters, and it has to
    # know this show (see "Over ssh" in the readme: a stale binary there
    # answers `unknown pattern` to a cue).
    echo "host: $HOST - the show runs there, the window is here"

fi

if [ -n "$REMOTE" ]; then
    # Batch mode, because a password prompt inside a pipe is a hang rather
    # than a question. Checked now rather than discovered thirty seconds in
    # with a window already open.
    if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "$REMOTE" true 2>/dev/null; then
        echo "warning: 'ssh $REMOTE true' did not succeed - the set will not start." >&2
        echo "         ssh runs in batch mode here, so it needs a key: a password" >&2
        echo "         prompt inside a pipe is a hang rather than a question." >&2
        # Is the bare form of this name an alias in ~/.ssh/config? Tokenised
        # rather than matched with a pattern, because `Host` takes a list and
        # a name is a whole word in it - a regex here matches `scanner-pi` in
        # `scanner-pi-two` and says the wrong thing confidently.
        if awk -v want="${REMOTE%%.*}" '
                tolower($1) == "host" {
                    for (i = 2; i <= NF; i++) if ($i == want) found = 1
                }
                END { exit !found }
           ' ~/.ssh/config 2>/dev/null; then
            echo "         ~/.ssh/config has a Host block named '${REMOTE%%.*}' - use that" >&2
            echo "         instead of '$REMOTE'. The alias is what carries the user and" >&2
            echo "         the key; the hostname on its own walks straight past both." >&2
        else
            echo "         check ~/.ssh/config for an alias for this machine (its Host" >&2
            echo "         name, not its hostname, is what carries the user and key)," >&2
            echo "         or set one up: ssh-keygen -t ed25519 && ssh-copy-id $REMOTE" >&2
        fi
    fi
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

if [ "$live" = 1 ] && [ -n "$REMOTE" ]; then
    # The widget, the obelisk and the ring are all on the host. Listing this
    # machine's serial ports would be answering a question nobody asked, and
    # a "nothing plugged in" warning here would be actively misleading.
    echo "wires: on $REMOTE - nothing on this machine is used"
fi

if [ "$live" = 1 ] && [ -z "$REMOTE" ]; then
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

# MIDI does not cross the ssh link. The executable is what reads a port, and
# over there it is the *host's* ALSA - so the Launchpad on this desk is
# invisible to a show on the pi, and a port named here would be looked for
# there. A named port that is missing is fatal at startup, so defaulting them
# on would turn "run the set on the pi" into "the set does not start".
#
# Defaulted, they go quiet and say so. Named, they are believed and passed
# through, because naming one over ssh can only mean the controller is plugged
# into the host - which is a real way to run this, just not the usual one.
if [ -n "$HOST" ]; then
    if [ "$midi_named" = 0 ] && [ -n "$MIDI_PORT" ]; then
        echo "pads:  off - MIDI does not cross ssh; the beat and the cue pads"
        echo "       would be the host's. --midi NAME if the controller is on $HOST."
        MIDI_PORT=""
    fi
    if [ "$midi_out_named" = 0 ] && [ -n "$MIDI_OUT" ]; then
        MIDI_OUT=""
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
if [ -z "$HOST" ] && [ -n "$EXE" ] && { [ -n "$MIDI_PORT" ] || [ -n "$MIDI_OUT" ]; }; then
    midi_ports="$("$EXE" --list-midi 2>/dev/null || true)"
fi

# Over ssh there is nothing here to check against - the ports that matter are
# the host's - so a named port is taken on trust rather than refused by a
# probe that was asking the wrong machine.
has_midi_port() {
    [ -n "$HOST" ] && return 0
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
    # The port. config/mythos26.json declares "audio": {"source": "synesthesia"}
    # and would open it on its own; naming it here is what makes
    # $ECLIPSE_OSC_IN_PORT and --osc-in override that. A port already held is a
    # warning, not a refusal - see _osc_input_for.
    command+=(--osc-in "$OSC_IN_PORT")
else
    # Said rather than left out. Since the config declares the source, leaving
    # the flag off no longer means "off" - it means "whatever the config says",
    # which is exactly what --no-osc-in is being asked to override.
    command+=(--no-osc-in)
fi

if [ -n "$HOST" ]; then
    command+=(--host "$HOST")
fi

if [ -n "$CLIENT" ]; then
    command+=(--client "$CLIENT")
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
