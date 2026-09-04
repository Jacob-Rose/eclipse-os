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
GRID = [lp.pad(row, col) for row in range(1, 9) for col in range(1, 9)]
# Overflow goes to the top row, which sits directly above grid row 8 - so the
# machine that spills off the top of the grid carries straight on.
OVERFLOW = list(lp.TOP_ROW)
# The scene-launch column: one button per machine, the classic bank selector.
SELECTORS = list(lp.RIGHT_COLUMN)

rows = []
slots = GRID + OVERFLOW
placed = 0

for machine in ORDER:
    for state in machines[machine]:
        index = slots[placed]
        placed += 1
        # A pad has to work from any machine, so it loads the machine first
        # and then opens the look. Re-issuing the pattern it is already on is
        # free; leaving it out means the pad only works when scanner happens
        # to be up already.
        rows.append(mm.Mapping(
            label=f"{machine} - {state}",
            kind="note" if index in lp.GRID else "cc",
            channel=1, number=index, mode="press",
            colour=COLOUR[machine],
            actions=[mm.Action("pattern", {"name": machine}),
                     mm.Action("state", {"name": state})]))

for machine, index in zip(ORDER, SELECTORS):
    # Pattern only: this one lights whenever its machine is loaded, whatever
    # look is running - which is what makes it read as "you are in this bank".
    rows.append(mm.Mapping(
        label=f"{machine} (machine)",
        kind="cc", channel=1, number=index, mode="press",
        colour=COLOUR[machine],
        actions=[mm.Action("pattern", {"name": machine})]))

if placed > len(slots):
    raise SystemExit(f"{placed} states will not fit in {len(slots)} buttons")

out = pathlib.Path(args.out)
mm.MappingSet(rows).save(out)

on_grid = min(placed, len(GRID))
overflowed = max(0, placed - len(GRID))
print(f"{len(rows)} rows -> {out}")
print(f"  {placed} state pads: {on_grid} on the grid"
      + (f", {overflowed} on the top row" if overflowed else ""))
print(f"  {len(ORDER)} machine selectors on the right column")
print(f"  spare: {len(OVERFLOW) - overflowed} of the top row, "
      f"{len(SELECTORS) - len(ORDER)} of the right column")
