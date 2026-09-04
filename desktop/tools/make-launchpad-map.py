"""Build a midimap that puts every state of every machine on a Launchpad X.

A script rather than a hand-written file, because it is seventy-odd rows of
JSON that have to agree with what the rig actually announces - and the rig is
the only authority on that. Add a state to a machine and this regenerates;
type it into the JSON by hand and it is right until the next time.

    ./build/eclipse-dmx --list-patterns          # what machines exist
    python tools/make-launchpad-map.py --probe   # ask the rig, then write

The surface it targets is a Launchpad X in Programmer mode: an 8x8 grid of
notes 11-88, a top row of control changes 91-98, and a right column of
89/79/.../19. Every state pad loads its machine *and* opens the look, in that
order, so a pad works from wherever the rig happens to be.
"""
import argparse, json, pathlib, subprocess, sys
from eclipse_dmx import launchpad as lp
from eclipse_dmx import midi_map as mm

MACHINES = ["mythos26", "jacket", "obelisk", "uv", "generic", "scanner"]


def probe(executable, config):
    """Ask the rig for each machine's looks. The only authority there is."""
    script = "\n".join(f"pattern {m}\nstates" for m in MACHINES) + "\nquit\n"
    raw = subprocess.run(
        [executable, "--config", config, "--dry-run", "--frames", "8000"],
        input=script, capture_output=True, text=True).stdout

    found, current = {}, None
    for line in raw.splitlines():
        if line.startswith("OK pattern "):
            current = line[len("OK pattern "):].strip()
        elif line.startswith("STATES") and current:
            found[current] = line.split()[1:]
            current = None
    missing = [m for m in MACHINES if not found.get(m)]
    if missing:
        raise SystemExit(f"no states came back for: {', '.join(missing)}")
    return found


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("out", nargs="?",
                    default="config/midimaps/launchpad-all-states.json")
parser.add_argument("--probe", action="store_true",
                    help="ask the executable for the states (default: --states-from)")
parser.add_argument("--states-from", metavar="JSON",
                    help="a {machine: [states]} file, instead of probing")
parser.add_argument("--executable", default="build/eclipse-dmx")
parser.add_argument("--config", default="config/mythos26.json")
args = parser.parse_args()

if args.states_from:
    machines = json.loads(pathlib.Path(args.states_from).read_text())
else:
    machines = probe(args.executable, args.config)

# Order is where each machine lands. mythos26 first puts tonight's show at the
# bottom left, where a hand starts; scanner last because it is the biggest and
# is the one that spills off the top of the grid onto the row above it.
#
# The five small machines happen to total exactly forty - five full rows - so
# scanner begins on a row boundary. That is luck rather than design, and the
# only boundary available: no other arrangement of 7/12/3/3/15 lands on a
# multiple of eight. Everything else wraps mid-row like text, which is what
# the colours are for.
ORDER = MACHINES

COLOUR = {
    "mythos26": lp.Colour.CYAN,
    "jacket":   lp.Colour.AMBER,
    "obelisk":  lp.Colour.BLUE,
    "uv":       lp.Colour.PURPLE,
    "generic":  lp.Colour.GREEN,
    "scanner":  lp.Colour.MAGENTA,
}

# The grid, bottom row first and left to right - the order a hand reads it.
# Every page gets the whole grid, because a page is one machine and the
# biggest machine is twenty-six looks.
GRID = [lp.pad(row, col) for row in range(1, 9) for col in range(1, 9)]

# The right column - the scene-launch buttons - are the tabs. One per machine,
# always on the surface whatever page is showing.
TABS = list(lp.RIGHT_COLUMN)

# The top row is *not* free real estate. Measured off the device in Programmer
# mode: 91 Up, 92 Down, 93 Left, 94 Right, then 95 Session, 96 Note, 97 Custom,
# 98 Capture MIDI. The arrows are hardware with a meaning, and burying a look
# under one is how a surface stops being readable.
UP, DOWN, LEFT, RIGHT = 91, 92, 93, 94

rows = []

for machine in ORDER:
    states = machines[machine]
    if len(states) > len(GRID):
        raise SystemExit(f"{machine} has {len(states)} looks; the grid holds {len(GRID)}")
    for index, state in zip(GRID, states):
        # The pattern action is kept even though the tab already switched the
        # machine: it costs nothing to re-issue, and it means a pad still does
        # the right thing if the rig is moved from the desk while a page is up.
        rows.append(mm.Mapping(
            label=f"{machine} - {state}",
            kind="note", channel=1, number=index, mode="press",
            page=machine, colour=COLOUR[machine],
            actions=[mm.Action("pattern", {"name": machine}),
                     mm.Action("state", {"name": state})]))

for machine, index in zip(ORDER, TABS):
    # A tab changes the surface *and* the rig - the page so the grid shows this
    # machine's looks, the pattern so the rig is on the machine those looks
    # belong to. Both, because either alone is a lie: a page whose states the
    # rig cannot take, or a machine whose looks are not on the surface.
    rows.append(mm.Mapping(
        label=f"{machine} (tab)",
        kind="cc", channel=1, number=index, mode="press",
        page="", colour=COLOUR[machine],
        actions=[mm.Action("page", {"name": machine}),
                 mm.Action("pattern", {"name": machine})]))

# The two momentary inputs a look reads - a jacket's remote buttons, on a rig
# that has none. Value mode, so the arrow is held rather than latched: a button
# sends full going down and zero coming up, and one mapping covers both edges.
for channel, index, colour in (("a", UP, lp.Colour.WHITE), ("b", DOWN, lp.Colour.WHITE)):
    rows.append(mm.Mapping(
        label=f"input {channel}",
        kind="cc", channel=1, number=index, mode="value",
        page="", colour=colour,
        actions=[mm.Action("input", {"channel": channel})]))

out = pathlib.Path(args.out)
mm.MappingSet(rows, opens_on=ORDER[0]).save(out)

pages = {m: sum(1 for r in rows if r.page == m) for m in ORDER}
print(f"{len(rows)} rows -> {out}")
print(f"  opens on '{ORDER[0]}'")
for machine, count in pages.items():
    print(f"    page {machine:<10} {count:>2} looks")
print(f"  {len(ORDER)} tabs on the right column, {len(TABS) - len(ORDER)} spare")
print(f"  inputs a/b on Up ({UP}) and Down ({DOWN}); "
      f"Left ({LEFT}) and Right ({RIGHT}) left alone")
