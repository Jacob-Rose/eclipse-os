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

Two files come out. The all-states map is every machine, a tab each. The
show's own map - midimaps/<config>.json, config/midimaps/mythos-show.json for
the set - is the show's page alone: sixteen cue pads off the config's cue
table, the intensity row, a row per layer, and nothing to tab to. That one is
what launch-mythos-set.sh runs on.
"""
import argparse, json, pathlib, subprocess, sys
from eclipse_dmx import launchpad as lp
from eclipse_dmx import midi_map as mm
from eclipse_dmx.config import Config, cue_actions

MACHINES = ["mythos26", "jacket", "obelisk", "uv", "flash", "generic", "scanner", "audio"]

#: The page the surface comes up on: the show, which is what the config
#: opens on too.
OPENS_ON = "mythos26"

#: The show's machine - the one page that is a cue list rather than a list of
#: looks. Its pads carry whole cues off the config's cue table, and the rest
#: of its grid is the show's controls; see the block below.
SHOW = "mythos26"

#: The three intensity pads on the show's page, as `param intensity` values.
#: Every show look has the knob - see Pattern_MythosLook - so one row of three
#: means the same thing on every cue.
INTENSITIES = [("low", 0.33, lp.Colour.AMBER), ("mid", 0.66, lp.Colour.YELLOW),
               ("high", 1.0, lp.Colour.WHITE)]


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
parser.add_argument("--show-out", metavar="JSON",
                    help="where the show's own map goes: its page alone, no tabs "
                         "(default midimaps/<config name>.json beside the config)")
parser.add_argument("--probe", action="store_true",
                    help="ask the executable for the states (default: --states-from)")
parser.add_argument("--states-from", metavar="JSON",
                    help="a {machine: [states]} file, instead of probing")
parser.add_argument("--executable", default="build/eclipse-dmx")
parser.add_argument("--config", default="config/mythos-show.json",
                    help="the show: its cue table and layers are what the "
                         "show's page is built from")
args = parser.parse_args()

if args.states_from:
    machines = json.loads(pathlib.Path(args.states_from).read_text())
else:
    machines = probe(args.executable, args.config)

show_config = Config.load(args.config)

# Order is where each machine lands. mythos26 first puts tonight's show at the
# bottom left, where a hand starts; scanner last because it is the biggest and
# is the one that spills off the top of the grid onto the row above it.
#
# Every page gets the whole grid, so a machine's looks wrap mid-row like text
# and the colours are what tell one page from the next. audio is last, and so
# takes the last tab: it is an instrument rather than a look, and the tab a
# hand finds first should be a show.
ORDER = MACHINES

COLOUR = {
    "mythos26": lp.Colour.CYAN,
    "jacket":   lp.Colour.AMBER,
    "obelisk":  lp.Colour.BLUE,
    "uv":       lp.Colour.PURPLE,
    "flash":    lp.Colour.PINK,
    "generic":  lp.Colour.GREEN,
    "scanner":  lp.Colour.MAGENTA,
    "audio":    lp.Colour.WHITE,
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
        #
        # On the show's page a pad is the whole cue: the state, then what the
        # config's cue table puts with it - each layer's state, the
        # visualiser's scene and media. The table is the authority; a state it
        # does not mention is the state alone.
        if machine == SHOW and show_config.pattern.name == SHOW:
            actions = [mm.Action.from_dict(entry) for entry in cue_actions(show_config, state)]
        else:
            actions = [mm.Action("pattern", {"name": machine}),
                       mm.Action("state", {"name": state})]
        rows.append(mm.Mapping(
            label=f"{machine} - {state}",
            kind="note", channel=1, number=index, mode="press",
            page=machine, colour=COLOUR[machine],
            actions=actions))

# The show's controls, on the rows above its cues. Sixteen cues fill the
# bottom two rows; the third is left dark so the controls read as controls.
#
#   row 4   intensity: low / mid / high, on whatever cue is up
#   row 5+  one row per layer the config declares, a pad per state, so the
#           flash and the UV can be moved off a cue's default by hand -
#           and put back by hitting the cue again
show_rows = len(machines[SHOW]) // 8 + (1 if len(machines[SHOW]) % 8 else 0)
controls_row = show_rows + 2
for col, (label, level, colour) in enumerate(INTENSITIES, start=1):
    rows.append(mm.Mapping(
        label=f"{SHOW} - intensity {label}",
        kind="note", channel=1, number=lp.pad(controls_row, col), mode="press",
        page=SHOW, colour=colour,
        actions=[mm.Action("param", {"name": "intensity", "low": 0.0, "high": level,
                                      "layer": ""})]))

for offset, layer in enumerate(show_config.layers, start=1):
    layer_states = machines.get(layer["pattern"], [])
    if not layer_states:
        print(f"  (layer '{layer['name']}' runs '{layer['pattern']}', which has no states; no pads)")
        continue
    if len(layer_states) > 8:
        raise SystemExit(f"layer '{layer['name']}' has {len(layer_states)} states; a row holds 8")
    row = controls_row + offset
    if row > 8:
        raise SystemExit(f"layer '{layer['name']}' would land on row {row}; the grid has 8")
    for col, state in enumerate(layer_states, start=1):
        rows.append(mm.Mapping(
            label=f"{SHOW} - {layer['name']} {state}",
            kind="note", channel=1, number=lp.pad(row, col), mode="press",
            page=SHOW, colour=COLOUR.get(layer["pattern"], lp.Colour.WHITE),
            actions=[mm.Action("layer", {"layer": layer["name"], "name": state})]))

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
mm.MappingSet(rows, opens_on=OPENS_ON).save(out)

# The show's own map: its page and nothing else, named for the config it was
# built from and kept beside it. This is what the set runs on - a surface
# that is the cue list, with no tab to press first and no way to wander onto
# the jacket's looks mid-set. The rows are the same objects as above with the
# page cleared, so the two files cannot disagree about what a cue does.
show_out = pathlib.Path(args.show_out) if args.show_out else (
    pathlib.Path(args.config).parent / "midimaps" / (pathlib.Path(args.config).stem + ".json"))
show_rows_out = []
for row in rows:
    if row.page != SHOW:
        continue
    data = row.to_dict()
    data["page"] = ""
    show_rows_out.append(mm.Mapping.from_dict(data))
mm.MappingSet(show_rows_out, opens_on="").save(show_out)

pages = {m: sum(1 for r in rows if r.page == m) for m in ORDER}
print(f"{len(show_rows_out)} rows -> {show_out}  (the show alone)")
print(f"{len(rows)} rows -> {out}")
print(f"  opens on '{OPENS_ON}'")
for machine, count in pages.items():
    print(f"    page {machine:<10} {count:>2} pads")
print(f"  {SHOW}: {len(machines[SHOW])} cues, {len(show_config.cues)} of them in the cue table, "
      f"intensity on row {controls_row}, {len(show_config.layers)} layer rows above it")
print(f"  {len(ORDER)} tabs on the right column, {len(TABS) - len(ORDER)} spare")
print(f"  inputs a/b on Up ({UP}) and Down ({DOWN}); "
      f"Left ({LEFT}) and Right ({RIGHT}) left alone")
