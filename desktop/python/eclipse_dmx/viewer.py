"""A window that shows what the rig is doing.

The point of this is to watch relic patterns - the obelisk's own generators,
running as the same code that runs on the sculpture - land on *your* fixtures,
without the fixtures being plugged in. So the layout is not the obelisk's LED
geometry and not anything hardcoded: it comes out of the config file, from the
same `position` fields the patterns themselves are driven by. Point it at a
different rig and you get a picture of that rig.

The colours are read back out of the DMX universe by the executable and
streamed over `--emit-frames`, not recomputed here. That matters: a viewer fed
from the pattern would happily show a beautiful rig while the patch was wrong,
which is exactly the bug you want a viewer to catch. If a fixture is dark on
screen it is because its channels are dark on the wire.

    python -m eclipse_dmx view config/uking_par36_x10.json --pattern obelisk_seasons

tkinter, so there is nothing to install.
"""

from __future__ import annotations

import math
import os
import threading
import time
import tkinter as tk
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from tkinter import colorchooser, simpledialog
from typing import Dict, List, Optional, Sequence, Tuple, Union

from . import look_presets
from .config import RELIC_TYPES, Config, cue_actions
from .controller import Frame, ShowController, ShowError
from .curve_editor import CurveEditor
from .midi_map import (Action, ActionContext, Dispatcher, MappingSet,
                       SynesthesiaState, parse_midi_line)
from .osc_input import DEFAULT_INPUT_PORT
from .launchpad import LampPainter, programmer_mode, clear as lamp_clear
from .midi_panel import MidiMapPanel
from .osc_panel import OscPanel, OscTraffic
from .patterns import list_patterns

RGB = Tuple[int, int, int]

#: WM class the viewer's window announces itself under, so a window manager
#: can be told where to put it. tkinter title-cases this, so a compositor sees
#: "Eclipse-dmx". Overridden by ECLIPSE_DMX_WM_CLASS.
WM_CLASS = "eclipse-dmx"

#: Seconds to wait before saying the OSC input is not landing. Long enough that
#: a visualiser started alongside the show is not accused of being off while it
#: is still coming up.
OSC_SILENCE_GRACE = 8.0

# ===========================================================================
# The buttons. This is the bit to edit.
# ===========================================================================
#
# Two sources, and they compose. The *curated* groups below are hardcoded on
# purpose: a generated row gives every look equal weight and alphabetical
# order, and what you actually want at a desk is the four you are using
# tonight, first, with the names you call them. Then every state the running
# machine offers that no curated button covers gets a *generated* button, so
# a new machine's whole cue list is clickable before anyone edits this file -
# grouped by the prefix table further down.
#
# Groups collapse: click a header. A group none of whose states the running
# machine knows starts collapsed; one with live states starts open. A group
# toggled by hand stays where it was put.
#
# Each entry is (label, command). A command is one of:
#
#     ("state",   "campfire")     switch a state machine to that state
#     ("pattern", "obelisk_seasons")   switch the whole pattern
#     ("input",   "a")            hold one of the two momentary inputs while
#                                 the button is held down
#     ("beat",    "")             a beat, now - tap it in
#     ("align",   "")             the one is now - where the bar starts
#     ("bpm",     "128")          set the tempo outright
#
# A state the running pattern does not offer is dimmed rather than hidden, so
# a group can hold looks from more than one machine.

STATE_GROUPS: List[Tuple[str, List[Tuple[str, Tuple[str, str]]]]] = [
    # The audio bus, one cue per channel. An instrument rather than a look -
    # see edmx/audio_meter.h - and first because while the analysis wire is
    # being trusted it is the thing you actually want in front of you.
    #
    # Written out rather than generated from AUDIO_STATES so this table stays
    # one flat literal like every group around it; the executable is the
    # authority on the names and TheAudioMeter pins the two together.
    ("audio", [
        ("level", ("state", "level")),
        ("bass", ("state", "bass")),
        ("mid", ("state", "mid")),
        ("midhigh", ("state", "midhigh")),
        ("high", ("state", "high")),
        ("hits", ("state", "hits")),
        ("bass_hits", ("state", "bass_hits")),
        ("mid_hits", ("state", "mid_hits")),
        ("midhigh_hits", ("state", "midhigh_hits")),
        ("high_hits", ("state", "high_hits")),
        ("presence", ("state", "presence")),
        ("bass_presence", ("state", "bass_presence")),
        ("mid_presence", ("state", "mid_presence")),
        ("midhigh_presence", ("state", "midhigh_presence")),
        ("high_presence", ("state", "high_presence")),
        ("beat", ("state", "beat")),
        ("bpm", ("state", "bpm")),
        ("bpm_confidence", ("state", "bpm_confidence")),
        ("intensity", ("state", "intensity")),
        ("level_instant", ("state", "level_instant")),
        ("level_meter", ("state", "level_meter")),
        # not a channel: the show's beat look, parked on this list while
        # mythos26 is empty. Labelled with the state's own name like every
        # row above it - TheAudioMeter holds the two together.
        ("beat_pulse", ("state", "beat_pulse")),
    ]),
    # The show: sixteen cues, numbered as "pattern spec.txt" numbers them,
    # the unwritten ones still slots. Rename them here when they are renamed
    # in makeMythos26StateMachine(). A button here fires the state, and then
    # whatever config/mythos-show.json's cue table says goes with it - see
    # ViewerApp._fire_cue.
    ("mythos26", [
        ("1 neuron", ("state", "neuron")),
        ("2 geode", ("state", "geode")),
        ("3 rain", ("state", "rain")),
        ("4 fire", ("state", "fire")),
        ("5 glitch", ("state", "glitch")),
        ("6 tunnel", ("state", "tunnel")),
        ("7 blown", ("state", "blown")),
        ("8 punk", ("state", "punk")),
        ("slot 9", ("state", "slot_9")),
        ("10 canyon", ("state", "canyon")),
        ("slot 11", ("state", "slot_11")),
        ("slot 12", ("state", "slot_12")),
        ("slot 13", ("state", "slot_13")),
        ("slot 14", ("state", "slot_14")),
        ("slot 15", ("state", "slot_15")),
        ("slot 16", ("state", "slot_16")),
    ]),
    # The static pair moved here with the machine that holds them - the rest
    # of the generic list is left to the generated buttons.
    ("generic", [
        ("static b/w", ("state", "tv_static_mono")),
        ("static rgb", ("state", "tv_static")),
    ]),
    ("jacket", [
        ("void", ("state", "digital_void")),
        ("forest", ("state", "enchanted_forest")),
        ("turbines", ("state", "warp_turbines")),
        ("rainbow", ("state", "rainbow_road")),
        ("breathe", ("state", "breathe_with_me")),
        ("parrot", ("state", "parrot")),
        ("overload", ("state", "system_overload")),
        ("toxin", ("state", "cyber_toxin")),
        ("datamine", ("state", "datamine")),
        ("bluemagic", ("state", "blue_magic")),
        ("campfire", ("state", "campfire")),
        ("hitstop", ("state", "hitstop")),
    ]),
]

#: Where a generated button lands: (group, tag prefix), first match wins, and
#: anything unclaimed goes in "other". These are afterglow's state families
#: today; a machine this table has never heard of still gets every button,
#: just less tidily sorted.
GENERATED_STATE_GROUPS: List[Tuple[str, str]] = [
    ("boot", "none"),
    ("boot", "power_up"),
    ("boot", "boot"),
    ("idle", "scan_idle"),
    ("detected", "scan_item_detected"),
    ("success", "scan_item_success"),
    ("failure", "scan_item_failure"),
    ("playback", "audio_playback"),
    ("record", "record"),
    ("void", "void"),
]


def _generated_group(state: str) -> str:
    for group, prefix in GENERATED_STATE_GROUPS:
        if state.startswith(prefix):
            return group
    return "other"


#: Labels for tags the stripping rule below would mangle - a tag that *is*
#: its whole family would otherwise come out empty or keep its full length,
#: and these columns are paid for by the pixel.
GENERATED_LABELS = {
    "power_up": "power up",
    "scan_idle": "idle",
    "scan_item_failure": "failure",
}


def _generated_label(state: str, group: str) -> str:
    """`scan_item_detected_mushroom_new` in group `detected` -> `mushroom new`.

    The group header already says the family, so the button says only what is
    left of the name.
    """
    if state in GENERATED_LABELS:
        return GENERATED_LABELS[state]

    remainder = state
    for candidate, prefix in GENERATED_STATE_GROUPS:
        if candidate == group and state.startswith(prefix):
            remainder = state[len(prefix):].lstrip("_")
            break
    return (remainder or state).replace("_", " ")

#: Held down, not toggled - these are momentary, like the remote buttons the
#: jacket's looks were written around.
INPUT_BUTTONS: List[Tuple[str, str]] = [
    ("input A", "a"),
    ("input B", "b"),
]

#: Offered when the running pattern is not a state machine, so the window is
#: still useful for the plain looks.
PATTERN_BUTTONS: List[Tuple[str, Tuple[str, str]]] = [
    # the machines: click one and its cue list fills the band above.
    # `scanner` is the game itself - every state the tower can be in, named
    # with its own tags - and the one a scanner config opens on, so it is
    # first: it is the way back from the others. `generic` is the WLED
    # recreations and the other free-standing stage looks (matrix rain,
    # fire, flow, lake, rainbow, chase, ...); `obelisk` is the sculpture's
    # own ambient looks (seasons, blobs, mono).
    ("scanner", ("pattern", "scanner")),
    ("generic", ("pattern", "generic")),
    ("obelisk", ("pattern", "obelisk")),
    ("mythos26", ("pattern", "mythos26")),
    ("jacket", ("pattern", "jacket")),
    # The audio bus, one cue per channel. A machine like the others, so it
    # gets a button like the others - and the cue band above fills with the
    # twenty-one channels the moment it is pressed.
    ("audio", ("pattern", "audio")),
    ("identify", ("pattern", "identify")),
]

#: Tempo, for when there is no MIDI or the phase has drifted. "tap" is a beat
#: now - tap it in, or shove a running clock back into time.
#:
#: "the one" is the other half, and it is the one to reach for when a look on
#: half or quarter time is hitting on the wrong beat of the bar: hit it on the
#: one and every slow rate lines up behind it. Mixxx says nothing about bars,
#: so until someone does this the rig is assuming.
TEMPO_BUTTONS: List[Tuple[str, Tuple[str, str]]] = [
    ("tap", ("beat", "")),
    ("the one", ("align", "")),
    ("100", ("bpm", "100")),
    ("120", ("bpm", "120")),
    ("128", ("bpm", "128")),
    ("140", ("bpm", "140")),
]

#: For a show driving a relic over USB. "pixels" renders here and streams the
#: frame; "cue" gives the sculpture its own looks back and only names them.
#: "release" hands the pixels over without leaving pixel mode - the button to
#: reach for when a look needs to come off the sculpture right now.
#:
#: Dimmed and inert on a show that is not on a relic link.
LINK_BUTTONS: List[Tuple[str, Tuple[str, str]]] = [
    ("pixels", ("link", "pixels")),
    ("cue", ("link", "cue")),
    ("release", ("link", "release")),
]

# ===========================================================================

# A dark room, so the lights are the brightest thing on screen.
BACKGROUND = (11, 13, 16)
PANEL = "#14181d"
#: The border and title bar of a device panel — enough to read as a window
#: edge against the dark room without competing with the lights.
PANEL_EDGE = "#2a323b"
TEXT = "#c8d0d8"
TEXT_DIM = "#6b7783"
TEXT_WARN = "#e8a33d"

BUTTON_BG = "#232a32"
BUTTON_BG_ACTIVE = "#3d6ea5"
BUTTON_FG = "#c8d0d8"

#: Rings drawn around each fixture to fake a beam. tkinter has no alpha, so the
#: falloff is opaque circles blended toward the background by hand.
GLOW_RINGS = 8
GLOW_EXTENT = 2.6  # outermost ring, in multiples of the fixture radius

#: Past this many nodes the rig is drawn as bare pixels: one dot each, no glow,
#: no per-node labels.
#:
#: Not a style choice. A glow is nine canvas items, and tk recolours items one
#: at a time from python - so a 344-pixel relic would be 3096 itemconfig calls
#: per repaint, which does not fit in a frame and drags the whole window down.
#: It is also the wrong picture: glows exist to suggest beams from fixtures
#: that light a room, and a strip is a strip. You want to read the shape of the
#: thing, and at this density the dots make the shape on their own.
DENSE_ABOVE = 64


def _blend(color: RGB, background: RGB, alpha: float) -> str:
    """`color` over `background` at `alpha`, as a tk hex string."""
    return "#%02x%02x%02x" % tuple(
        max(0, min(255, int(round(c * alpha + b * (1.0 - alpha)))))
        for c, b in zip(color, background)
    )


def _run_name(name: str) -> str:
    """`a_up_17` -> `a_up`: the run a node belongs to.

    Bulk patching appends `_1`, `_2`, ... to a bank's name, so dropping that
    suffix recovers the name the config actually wrote.
    """
    head, sep, tail = name.rpartition("_")
    return head if sep and tail.isdigit() else name


class CueGroup:
    """One collapsible family of cue buttons: a column in the cue band.

    Twenty-six states in flat rows is a wall. As columns side by side - the
    family name on top, its cues stacked under it - the whole machine sits in
    one horizontal band and reads like a menu. The header is the toggle:
    click it and the family folds to just its name, and the count says how
    much a folded column is hiding.
    """

    #: Buttons per column before a family spills into a second one. Six caps
    #: a column's height at the band the window already pays for.
    PER_COLUMN = 6

    def __init__(self, parent: tk.Widget, title: str, on_toggle=None):
        self.title = title
        #: The state names this group's buttons drive - what "live" means.
        self.states: set = set()

        self.frame = tk.Frame(parent, bg=PANEL)
        self.header = tk.Label(
            self.frame, text="", anchor="w", cursor="hand2",
            bg=PANEL, fg=TEXT_DIM, font=("Consolas", 9), padx=4,
        )
        self.header.pack(fill="x")
        self.header.bind("<Button-1>", lambda event: self.toggle())

        self.body = tk.Frame(self.frame, bg=PANEL)
        self.body.pack(fill="x", padx=4)

        self.collapsed = False
        self.buttons: List[tk.Button] = []
        self._on_toggle = on_toggle
        self._refresh_header()

    def add_button(self, label: str, command: Tuple[str, str], runner) -> tk.Button:
        button = tk.Button(
            self.body, text=label, font=("Consolas", 9),
            bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
            highlightthickness=0, borderwidth=0,
            command=lambda c=command: runner(c),
        )
        index = len(self.buttons)
        button.grid(row=index % self.PER_COLUMN, column=index // self.PER_COLUMN,
                    sticky="ew", padx=2, pady=2)
        self.buttons.append(button)
        if command[0] == "state":
            self.states.add(command[1])
        self._refresh_header()
        return button

    def set_collapsed(self, collapsed: bool) -> None:
        if collapsed == self.collapsed:
            return
        self.collapsed = collapsed
        if collapsed:
            self.body.pack_forget()
        else:
            self.body.pack(fill="x", padx=4)
        self._refresh_header()

    def toggle(self) -> None:
        self.set_collapsed(not self.collapsed)
        if self._on_toggle:
            self._on_toggle(self)

    def _refresh_header(self) -> None:
        arrow = "▸" if self.collapsed else "▾"
        self.header.configure(text=f"{arrow} {self.title} ({len(self.buttons)})")

    def destroy(self) -> None:
        self.frame.destroy()


class CueSection:
    """A whole machine's cue list, sectioned off as one nest.

    The family columns sit inside it and the section is the outline around
    them, with the machine's name on the corner - so afterglow's twenty-six
    cues read as one block, cleanly apart from whatever else shares the band.
    Folding the section folds the machine down to its name.
    """

    def __init__(self, parent: tk.Widget, title: str, on_toggle=None):
        self.title = title
        self.frame = tk.Frame(parent, bg=PANEL, highlightthickness=1,
                              highlightbackground=PANEL_EDGE)
        self.header = tk.Label(
            self.frame, text="", anchor="w", cursor="hand2",
            bg=PANEL, fg=TEXT, font=("Consolas", 9, "bold"), padx=4,
        )
        self.header.pack(fill="x")
        self.header.bind("<Button-1>", lambda event: self.toggle())

        self.body = tk.Frame(self.frame, bg=PANEL)
        self.body.pack(fill="x", padx=2, pady=(0, 2))

        self.collapsed = False
        self.groups: List[CueGroup] = []
        self._on_toggle = on_toggle
        self._refresh_header()

    def set_collapsed(self, collapsed: bool) -> None:
        if collapsed == self.collapsed:
            return
        self.collapsed = collapsed
        if collapsed:
            self.body.pack_forget()
        else:
            self.body.pack(fill="x", padx=2, pady=(0, 2))
        self._refresh_header()

    def toggle(self) -> None:
        self.set_collapsed(not self.collapsed)
        if self._on_toggle:
            self._on_toggle(self)

    def _refresh_header(self) -> None:
        arrow = "▸" if self.collapsed else "▾"
        count = sum(len(group.buttons) for group in self.groups)
        self.header.configure(text=f"{arrow} {self.title} ({count})")

    def destroy(self) -> None:
        self.frame.destroy()


class DevicePanel:
    """One device's own little window inside the main one.

    An environment holds physical things - a pillar, a truss - and they do not
    share a shape, a scale or a sensible place on screen. Drawing them into one
    canvas means the 344-pixel sculpture squashes the ten pars into a corner.
    So each gets a panel: drag it by the title bar, size it by the grip in the
    bottom-right, and its fixtures lay themselves out into whatever room it
    ends up with.

    Panels are children of the desk surface rather than real OS windows, so they
    move with the main window, stay on top of it, and cannot be lost behind it.
    FL Studio's plugin windows work the same way and for the same reasons.
    """

    #: Grab area for the resize corner.
    GRIP = 14
    TITLE_HEIGHT = 20

    def __init__(self, surface: tk.Widget, name: str, placements: List["Placement"],
                 device=None, on_touched=None):
        self.name = name
        self.placements = placements
        self.surface = surface
        #: The config device behind this panel.
        self.device = device
        #: How much of the window this panel asks for, relative to an even
        #: share. Straight out of the environment; see Placement.view_scale.
        self.view_scale = list(device.placement.view_scale) if device else [1.0, 1.0]
        #: Called the first time this panel is moved or sized by hand.
        self._on_touched = on_touched

        self.frame = tk.Frame(surface, bg=PANEL, highlightthickness=1,
                              highlightbackground=PANEL_EDGE, highlightcolor=PANEL_EDGE)

        self.title = tk.Label(
            self.frame, text=f"  {name}  ({len(placements)})", anchor="w",
            bg=PANEL, fg=TEXT_DIM, font=("Consolas", 9), cursor="fleur",
        )
        self.title.pack(side="top", fill="x")

        #: Why this device has no wire, or "" when it has one.
        self.offline = ""

        self.canvas = tk.Canvas(self.frame, bg="#%02x%02x%02x" % BACKGROUND,
                                highlightthickness=0)
        self.canvas.pack(side="top", fill="both", expand=True)

        # The grip sits *over* the canvas rather than beside it, so it costs no
        # drawing room and lands where a resize handle is expected to be.
        self.grip = tk.Frame(self.frame, bg=PANEL_EDGE, cursor="bottom_right_corner",
                             width=self.GRIP, height=self.GRIP)
        self.grip.place(relx=1.0, rely=1.0, anchor="se")

        self._items: List[dict] = []
        self._painted: Optional[Frame] = None
        self._drag: Optional[Tuple[int, int]] = None

        for widget in (self.title,):
            widget.bind("<ButtonPress-1>", self._start_move)
            widget.bind("<B1-Motion>", self._on_move)
            widget.bind("<ButtonRelease-1>", self._end_drag)

        self.grip.bind("<ButtonPress-1>", self._start_resize)
        self.grip.bind("<B1-Motion>", self._on_resize)
        self.grip.bind("<ButtonRelease-1>", self._end_drag)

        self.canvas.bind("<Configure>", lambda event: self.rebuild())

    def set_offline(self, reason: str) -> None:
        """Marks this device as having no wire, in its own title bar.

        The panel keeps painting. That is the point of it: the look *is* being
        rendered for this device, and seeing it run is most of why you would
        open a viewer with the hardware unplugged. The title says the frames are
        not going anywhere, so the two are not confused for each other.
        """
        self.offline = reason
        if reason:
            self.title.configure(text=f"  {self.name}  ({len(self.placements)})  offline - {reason}",
                                 fg=TEXT_WARN)
        else:
            self.title.configure(text=f"  {self.name}  ({len(self.placements)})", fg=TEXT_DIM)

    # -- geometry ----------------------------------------------------------

    def place(self, x: int, y: int, width: int, height: int) -> None:
        self.frame.place(x=x, y=y, width=width, height=height)

    def _start_move(self, event: "tk.Event") -> None:
        self._drag = (event.x_root - self.frame.winfo_x(),
                      event.y_root - self.frame.winfo_y())
        self.frame.lift()
        if self._on_touched:
            self._on_touched()

    def _on_move(self, event: "tk.Event") -> None:
        if self._drag is None:
            return
        offset_x, offset_y = self._drag

        # Kept inside the surface, with a strip of the title always reachable:
        # a panel dragged past the edge would be unrecoverable without a way to
        # scroll to it, and there is no such way.
        limit_x = max(0, self.surface.winfo_width() - 40)
        limit_y = max(0, self.surface.winfo_height() - self.TITLE_HEIGHT)

        self.frame.place_configure(
            x=max(0, min(event.x_root - offset_x, limit_x)),
            y=max(0, min(event.y_root - offset_y, limit_y)),
        )

    def _start_resize(self, event: "tk.Event") -> None:
        self._drag = (event.x_root - self.frame.winfo_width(),
                      event.y_root - self.frame.winfo_height())
        self.frame.lift()
        if self._on_touched:
            self._on_touched()

    def _on_resize(self, event: "tk.Event") -> None:
        if self._drag is None:
            return
        offset_x, offset_y = self._drag
        self.frame.place_configure(
            width=max(120, event.x_root - offset_x),
            height=max(80, event.y_root - offset_y),
        )

    def _end_drag(self, event: "tk.Event") -> None:
        self._drag = None

    # -- drawing -----------------------------------------------------------

    def rebuild(self) -> None:
        """Lays this device's fixtures out for the panel's current size."""
        self.canvas.delete("all")
        self._items = []

        width = max(self.canvas.winfo_width(), 1)
        height = max(self.canvas.winfo_height(), 1)
        if width <= 1 or height <= 1 or not self.placements:
            return

        margin_x = max(width * 0.08, 6.0)
        margin_y = max(height * 0.10, 6.0)
        label_room = 14 if len(self.placements) <= DENSE_ABOVE else 12

        span_x = max(width - 2 * margin_x, 1.0)
        span_y = max(height - 2 * margin_y - label_room, 1.0)

        points = [
            (margin_x + place.x * span_x, margin_y + place.y * span_y)
            for place in self.placements
        ]

        if len(self.placements) > DENSE_ABOVE:
            self._build_dense(points)
        else:
            self._build_glowing(points, span_x, span_y, margin_x, margin_y)

        self._paint(self._painted, force=True)

    def _build_glowing(self, points, span_x, span_y, margin_x, margin_y) -> None:
        closest = min(span_x, span_y)
        for i, (ax, ay) in enumerate(points):
            for bx, by in points[i + 1:]:
                closest = min(closest, ((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5)

        radius = closest * 0.5 / GLOW_EXTENT
        radius = min(radius, margin_x / GLOW_EXTENT, margin_y / GLOW_EXTENT)
        radius = max(min(radius, 44.0), 3.0)

        for place, (cx, cy) in zip(self.placements, points):
            rings = []
            for index in range(GLOW_RINGS):
                t = index / max(GLOW_RINGS - 1, 1)
                ring_radius = radius * (GLOW_EXTENT - (GLOW_EXTENT - 1.0) * t)
                rings.append((
                    self.canvas.create_oval(
                        cx - ring_radius, cy - ring_radius,
                        cx + ring_radius, cy + ring_radius,
                        outline="", fill="#%02x%02x%02x" % BACKGROUND,
                    ),
                    0.05 + 0.95 * (t ** 2.4),
                ))

            core = self.canvas.create_oval(
                cx - radius, cy - radius, cx + radius, cy + radius,
                outline="", fill="#%02x%02x%02x" % BACKGROUND,
            )

            self.canvas.create_text(cx, cy + radius * GLOW_EXTENT + 10,
                                    text=place.name, fill=TEXT, font=("Consolas", 7))
            self._items.append({"rings": rings, "core": core})

    def _build_dense(self, points) -> None:
        gaps = [
            ((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5
            for (ax, ay), (bx, by) in zip(points, points[1:])
            if (ax - bx) ** 2 + (ay - by) ** 2 > 0.0
        ]
        radius = max(min((min(gaps) if gaps else 6.0) * 0.55, 9.0), 1.0)

        for (cx, cy) in points:
            self._items.append({
                "rings": (),
                "core": self.canvas.create_oval(
                    cx - radius, cy - radius, cx + radius, cy + radius,
                    outline="", fill="#%02x%02x%02x" % BACKGROUND,
                ),
            })

        runs: Dict[str, List[float]] = {}
        for place, (cx, _) in zip(self.placements, points):
            runs.setdefault(_run_name(place.name), []).append(cx)

        # Only label the runs if the labels will read. Squeezed into a narrow
        # pillar of a panel they overlap into "a_updownb_updown" and are worse
        # than nothing - the shape is the information at that width, and the
        # panel's own title still says which device this is.
        widest = max((len(name) for name in runs), default=0)
        spacing = self.canvas.winfo_width() / max(len(runs), 1)
        if spacing < widest * 6 + 6:
            return

        label_y = max(y for _, y in points) + radius + 9
        for name, xs in runs.items():
            self.canvas.create_text(sum(xs) / len(xs), label_y, text=name,
                                    fill=TEXT_DIM, font=("Consolas", 7))

    def _paint(self, frame: Optional[Frame], force: bool = False) -> None:
        if not force and frame is self._painted:
            return
        self._painted = frame

        for index, item in enumerate(self._items):
            color: RGB = (0, 0, 0)
            if frame is not None and index < len(frame):
                color = frame[index]

            for oval, alpha in item["rings"]:
                self.canvas.itemconfig(oval, fill=_blend(color, BACKGROUND, alpha))
            self.canvas.itemconfig(item["core"], fill="#%02x%02x%02x" % color)

    def paint(self, frame: Optional[Frame]) -> None:
        """Takes this device's own slice of a whole-show frame."""
        self._paint(frame)

    def destroy(self) -> None:
        self.frame.destroy()


@dataclass
class Placement:
    """Where one fixture sits on screen, and what to call it."""

    name: str
    address: int
    x: float  # 0..1 across the canvas
    y: float  # 0..1 down the canvas


def _slider_step(minimum: float, maximum: float) -> float:
    """A resolution that gives a slider a useful number of stops.

    A hundred steps across whatever range the property declared, rounded down
    to a power of ten so the numbers a slider lands on read as numbers -
    0.01 and 0.1 rather than 0.0287. The box beside it is there for the values
    between.
    """
    span = abs(maximum - minimum)
    if span <= 0.0:
        return 0.001

    step = span / 100.0
    magnitude = 10.0 ** math.floor(math.log10(step))
    return max(magnitude, 0.001)


def plan_layout(config: Config) -> List[Placement]:
    """Reads the rig's shape out of the config.

    `position` is the same field the executable uses to place fixtures in the
    pattern's coordinate space, so the picture and the light agree about which
    fixture is where. Positions are normalised rather than taken literally,
    which means they can be written in whatever units the rig was measured in.

    A fixture with no position falls back to its patch order, so a config that
    never mentions position still draws as the row it almost certainly is.
    """
    fixtures = config.fixtures
    count = len(fixtures)

    raw: List[Tuple[float, float]] = []
    for index, fixture in enumerate(fixtures):
        position: Optional[Sequence[float]] = fixture.position
        if position:
            x = float(position[0])
            y = float(position[1]) if len(position) > 1 else 0.0
        else:
            x = 0.0 if count == 1 else index / (count - 1)
            y = 0.0
        raw.append((x, y))

    def normalise(values: Sequence[float]) -> List[float]:
        low, high = min(values), max(values)
        if high - low < 1e-6:
            # every fixture on the same line: centre it rather than divide by ~0
            return [0.5 for _ in values]
        return [(value - low) / (high - low) for value in values]

    xs = normalise([point[0] for point in raw])
    # y is up in the configs - the obelisk's floor is y=0 and the ring sits
    # up on it - and down on a canvas, so it flips here. Rigs with no
    # vertical extent (a row of pars) normalise to a centred 0.5 and never
    # notice.
    ys = [1.0 - value for value in normalise([point[1] for point in raw])]

    return [
        Placement(
            name=fixture.name,
            address=config.display_channel(fixture.start_channel),
            x=x,
            y=y,
        )
        for fixture, x, y in zip(fixtures, xs, ys)
    ]


class ViewerApp:
    """The window, and the show behind it."""

    def __init__(
        self,
        config_path: Union[str, Path],
        executable: Optional[Union[str, Path]] = None,
        pattern: Optional[str] = None,
        state: Optional[str] = None,
        live: bool = False,
        emit_rate: float = 30.0,
        midi: Optional[str] = None,
        bpm: Optional[float] = None,
        osc=None,
        osc_device: Optional[str] = None,
        osc_fixture: int = 0,
        midimap: Optional[Union[str, Path]] = None,
        midi_out: Optional[str] = None,
        client: Optional[str] = None,
        oscmap: Optional[Union[str, Path]] = None,
        osc_in: Optional[int] = None,
        host: Optional[str] = None,
        remote_command: Optional[str] = None,
        # Wide enough for the whole cue band - every family of a 26-state
        # machine as a column - without folding anything.
        width: int = 1200,
        # Room for one band of cue columns under the canvas. The knobs sit
        # beside them rather than below, so this does not grow with them.
        height: int = 620,
    ) -> None:
        # The picture is drawn from the local copy of the config, whether the
        # show runs here or on `host`: the far end reads the same file out of
        # the same checkout, and what the executable actually put in a frame
        # comes back over the stream either way.
        self.config_path = Path(config_path)
        self.config = Config.load(self.config_path)
        self.config.validate(strict_overlap=False)

        self.placements = plan_layout(self.config)
        self.live = live
        self.host = host

        # -- the screen is not an LED ---------------------------------------
        # A frame arrives with master.gamma already applied, because an LED is
        # linear in duty cycle and an eye is not. A monitor corrects again all
        # by itself, so painting the raw channels applies the curve twice and
        # everything below mid-brightness crushes toward black - the dim looks
        # (a breathing floor of 0.15, a void stone at a third) read as not
        # animating at all, while the same frames look right on the sculpture.
        # Undo it exactly once, for the same reason the OSC sender does; a
        # 256-entry table because this runs per channel per frame.
        gamma = float(getattr(self.config.master, "gamma", 1.0) or 1.0)
        self._ungamma: Optional[List[int]] = None
        if abs(gamma - 1.0) > 1e-3:
            self._ungamma = [
                min(255, int(round(((value / 255.0) ** (1.0 / gamma)) * 255.0)))
                for value in range(256)
            ]

        try:
            self.pattern_names = list_patterns(executable)
        except Exception:
            # Not being able to list patterns is not worth refusing to draw
            # over; it only costs the ability to cycle them with a keypress.
            self.pattern_names = []

        # -- frame hand-off ------------------------------------------------
        # The reader thread only ever drops the newest frame in a slot, and the
        # tk loop picks it up. Latest-wins on purpose: a queue would build lag
        # on a slow redraw, and a stale frame is worth nothing to a viewer.
        self._lock = threading.Lock()
        self._latest: Optional[Frame] = None
        self._frames_seen = 0

        #: The frame currently on the canvas, so a repaint of it can be skipped.
        self._painted: Optional[Frame] = None

        #: How many lines the relic has sent, so the note beside the link
        #: buttons is refreshed when it says something and not every pump.
        self._relic_said = 0

        #: One panel per device. Built when the executable announces them.
        self._panels: List[DevicePanel] = []
        self._panel_signature: Tuple = ()

        #: Narrowest a panel is tiled to. Below this the labels stop being
        #: readable and dragging it becomes fiddly, at which point it is not a
        #: window any more.
        self.MIN_PANEL = 130

        #: Set the moment a panel is dragged or sized. After that the window
        #: stops re-tiling on resize, because a hand-placed layout is worth more
        #: than an even one.
        self._panels_touched = False

        self._fps = 0.0
        self._fps_marker = time.monotonic()
        self._fps_counted = 0

        self._status = ""
        self._blackout = False
        self._master = self.config.master.brightness

        # Set before the window exists: closing it during startup still has to
        # find a coherent teardown path.
        self._closing = False
        self._pump_id: Optional[str] = None

        #: last (states, current) the buttons were drawn for
        self._button_signature: Tuple[Tuple[str, ...], str] = ((), "")

        # -- the colour, out to a visualiser -------------------------------
        #
        # The same frames the canvas is painted from, put in a UDP packet. One
        # process and one render for both, which is the point of it being here
        # rather than beside it: `osc` and `view` as two commands means two
        # shows, each with its own beat clock, drifting apart from the moment
        # they start - and the screen behind a rig showing the colour some
        # *other* copy of the rig is at is worse than showing none.
        #
        # Optional and fire-and-forget: no link, no cost, and a link that
        # cannot send never reaches the window. See eclipse_dmx/osc.py.
        self._osc = osc
        self._osc_device = osc_device
        self._osc_fixture = osc_fixture
        self._osc_resolved = False

        # -- midi mappings, pads bound to cues and visuals ------------------
        #
        # Events arrive on the reader thread (via on_midi, below) and are
        # dispatched from _pump on the tk thread. Deliberately not fired from
        # the callback: an action that talks the protocol waits on a reply the
        # reader thread itself delivers, so firing there would deadlock the
        # desk on its first pad. A deque because appends are atomic and the
        # pump drains within a frame - latest-wins is wrong here, every press
        # counts.
        self._midi_events: "deque[object]" = deque(maxlen=256)

        self._midimap_dir = self.config_path.parent / "midimaps"
        midimap_path = Path(midimap) if midimap else self._midimap_dir / "default.json"
        self._midimap = MappingSet()
        self._midimap_note = ""
        if midimap_path.exists():
            try:
                self._midimap = MappingSet.load(midimap_path)
            except (OSError, ValueError, KeyError) as error:
                # a broken file must not refuse the window; say so instead
                self._midimap_note = f"midi map: {error}"
        elif midimap:
            self._midimap_note = f"midi map: {midimap_path.name} not found; starting empty"
        #: The lamps, when there is a controller to light. Built whatever
        #: happens - it costs nothing without a port, and the panel edits the
        #: same MappingSet it paints, so it must exist before the panel does.
        self._lamps = LampPainter(self._midimap)
        self._midi_out_wanted = (midi_out or "").strip()
        self._lamps_open = False
        #: Whether the input port has been handed back; see _release_devices.
        self._midi_released = False
        #: Set by a signal handler, acted on by the pump. See _install_signal_handlers.
        self._bail_requested = False

        #: where the panel saves without asking. A --midimap that does not
        #: exist yet is still the place its mappings should land.
        self._midimap_path: Optional[Path] = (
            midimap_path if (midimap or midimap_path.exists()) else None)

        #: the mapping actions' own link to the visualiser, made on first
        #: use when --osc did not already provide one
        self._map_link = None

        # -- look presets, a state's knobs kept as files --------------------
        # Beside the environment configs, like the midimaps: what a tuning
        # session leaves behind should live with the rig it was tuned on.
        self._preset_dir = self.config_path.parent / "looks"
        #: (state, names) the dropdown was last built for, so the pump can
        #: refresh it by comparing rather than re-listing a directory a
        #: hundred times a second
        self._preset_signature: Tuple = ()

        self.show = ShowController(
            self.config_path,
            executable=executable,
            dry_run=not live,
            on_frame=self._on_frame,
            on_midi=self._on_midi_line,
            emit_rate=emit_rate,
            midi=midi,
            bpm=bpm,
            # Opened on, rather than switched to after start. The executable
            # takes both, so asking it directly means the window's first frame
            # is already the look that was asked for - where switching
            # afterwards showed the config's look for a beat first, and left
            # the knobs announced a moment later than everything that reads
            # them expects.
            pattern=pattern,
            state=state,
            autostart=False,
            remote=host,
            remote_command=remote_command,
        )

        #: What the visualiser is up to, shared by both dispatchers. The pads
        #: and the audio engine run against separate contexts - they are built
        #: at different times and the OSC one may never exist - but a scene the
        #: *app* announced arrives on the OSC side while the lamp asking about
        #: it hangs off the MIDI side, so this one object has to span them.
        #: Client mode: the machine whose wires this desk drives. The show
        #: runs *here* - which is the whole point, because MIDI does not cross
        #: ssh and the controllers are on this desk - and the frames go there.
        self._client_host = (client or "").strip()
        self._sink: Optional[ShowController] = None
        self._sink_failed = False

        self._syn_state = SynesthesiaState()

        self._dispatcher = Dispatcher(self._midimap, ActionContext(
            show=self.show, osc_factory=self._map_osc, say=self._say,
            syn=self._syn_state))
        # Which tab the map opens on. A map with no pages says nothing here
        # and every row is on every page, which is how one without them works.
        self._dispatcher.context.page = self._midimap.opens_on

        # The other input: a visualiser's audio analysis, bound to the same
        # actions the pads use. Built here and opened after the show starts -
        # a binding that fires before there is anything to fire at would be a
        # refusal in the status line on the first bass note.
        self._osc_in_port = osc_in
        self._oscmap_path = (Path(oscmap) if oscmap
                             else self.config_path.parent / "oscmaps" / "synesthesia.json")
        self._oscmap_wanted = bool(osc_in is not None or oscmap)
        self._osc_listener = None
        self._osc_dispatcher = None

        #: Whose knobs the right-hand pane shows and the curve editor drives:
        #: the show's look, or one of its layers'. Both offer the same calls
        #: (params, curves, get/set_param, set_curve, reset_look, a revision),
        #: so everything below that tunes a look tunes whichever this is.
        self._knobs = self.show
        self._knob_target_buttons: Dict[str, tk.Button] = {}
        #: per layer, its state buttons by state name
        self._layer_buttons: Dict[str, Dict[str, tk.Button]] = {}
        self._layers_revision = -1

        self._build_window(width, height)

        self.show.start()

        # The monitor is what carries pad presses up to the mappings; on with
        # no port open it is silent and free, so it is simply always on.
        try:
            self.show.midi_monitor(True)
        except ShowError:
            pass
        # Already running: the show was started on it. This only records which
        # it is, for the surface.
        self.current_pattern = pattern or self.config.pattern.name

        if state:
            # The show already opened on it; this records it for the surface.
            # Kept as a guarded call rather than a bare assignment so a name
            # the machine does not have still leaves a working window with a
            # message, which is what it did before.
            self._guard(lambda: self.show.set_state(state), "state")

        # Both of these speak, and _say draws the header - which reads
        # `current_pattern`, so neither can happen before the cue above has set
        # it. Starting the OSC input here also means a binding cannot be
        # outrun by the state it is aimed at.
        if self._midimap_note:
            self._say(self._midimap_note)
        if self._client_host:
            self._open_sink(self._client_host)
        if self._midi_out_wanted:
            self._open_lamps(self._midi_out_wanted)
        if self._oscmap_wanted:
            self._start_osc_input()

        self._refresh_header()
        self._refresh_link_buttons()
        self._pump_id = self.root.after(16, self._pump)

    # -- window ------------------------------------------------------------

    def _build_window(self, width: int, height: int) -> None:
        # The window class is what a tiling compositor matches its rules on,
        # and tkinter's default is the bare "Tk" every python GUI shares. Name
        # it, so a rule can find this window and nothing else. The test suite
        # overrides the name so a run's windows can be parked out of the way
        # instead of landing on whatever workspace is in front - see
        # tests/harness.py, and "Tests" in the readme.
        self.root = tk.Tk(className=os.environ.get("ECLIPSE_DMX_WM_CLASS", WM_CLASS))
        where = f"  @ {self.host}" if self.host else ""
        self.root.title(f"eclipse-dmx  -  {self.config_path.name}{where}")
        self.root.configure(bg=PANEL)
        self.root.geometry(f"{width}x{height}")
        self.root.minsize(480, 280)

        self.header = tk.Label(
            self.root, anchor="w", bg=PANEL, fg=TEXT, font=("Consolas", 11), padx=12, pady=8
        )
        self.header.pack(fill="x")

        # -- the devices, as a row to look through --------------------------
        # One button per device plus "all": a device's button gives its panel
        # the whole surface, "all" restores the tiled desk. Populated once the
        # executable announces what is actually in the show, and only shown
        # when there is more than one panel - a choice with one answer is
        # furniture.
        self.device_row = tk.Frame(self.root, bg=PANEL)
        self._device_row_label = tk.Label(self.device_row, text="devices ", bg=PANEL,
                                          fg=TEXT_DIM, font=("Consolas", 9), padx=12)
        self._device_row_label.pack(side="left")
        self._device_buttons: Dict[Optional[str], tk.Button] = {}
        self._focus_device: Optional[str] = None

        # Packed from the bottom up, and the canvas last. The canvas is the only
        # thing that expands, so packing it first lets it claim the window and
        # push the controls off the bottom edge whenever they grow — which they
        # do, every time a look with more knobs comes up. Bottom-up, the
        # controls always get the height they asked for and the picture takes
        # what is left.
        self.footer = tk.Label(
            self.root,
            anchor="w",
            bg=PANEL,
            fg=TEXT_DIM,
            font=("Consolas", 9),
            padx=12,
            pady=6,
            text="[space] blackout   [n]/[p] pattern   [↑]/[↓] master   "
            "[←]/[→] speed   [t] tap the beat   [e] curves   [m] midi   [q] quit",
        )
        self.footer.pack(side="bottom", fill="x")

        self._build_buttons()

        # The desk surface: not a canvas of its own, just the room the device
        # panels live in. They are placed on it absolutely so they can be
        # dragged; it expands, so they stay put relative to the window when it
        # is resized rather than being re-tiled underneath the cursor.
        self.surface = tk.Frame(self.root, bg="#%02x%02x%02x" % BACKGROUND)
        self.surface.pack(fill="both", expand=True)

        self.surface.bind("<Configure>", lambda event: self._on_surface_resized())

        # -- the curve editor, folded away until asked for -------------------
        # A band between the desk and the buttons rather than a swap view, so
        # the rig stays on screen while a curve is being shaped at it - seeing
        # the animation land on the fixtures is the whole point of it.
        self.curve_editor = CurveEditor(
            self.root, on_send=self._animate_target, on_status=self._say,
            on_send_curve=self._push_look_curve,
            on_load_curve=self._look_curve_keys,
            on_override=self._hold_target)
        self._curves_shown = False
        #: (name, value) held while the editor's transport borrows a knob
        self._held_target = None

        # -- the midi map, folded away down the right edge -------------------
        # Beside the desk rather than under it: a binding is made while
        # watching the cue it fires, and the rig picture is the thing the
        # panel must not cover. Packed on toggle; see _toggle_midimap.
        self.midi_panel = MidiMapPanel(
            self.root, self._midimap, self._midimap_path, self._midimap_dir,
            on_status=self._say, on_open_midi=self._open_midi)
        self._midimap_shown = False

        # -- the osc watcher, likewise ---------------------------------------
        # Fed from both wire threads and drawn on the pump; see osc_panel. The
        # traffic store is built whether or not the panel is ever opened,
        # because what it is for is the question asked *after* something has
        # already gone wrong, and a recorder started then has nothing to show.
        self.osc_traffic = OscTraffic()
        self.osc_panel = OscPanel(
            self.root, self.osc_traffic,
            in_port=self._osc_in_port if self._oscmap_wanted else None,
            link_describe=self._describe_osc_out,
            link_stats=self._osc_out_stats,
            bindings_for=self._bindings_for_address,
            on_status=self._say)
        self._oscpanel_shown = False

        # Said once, when it becomes true. The whole failure mode of this wire
        # is silence - a binding aimed at an address that never arrives looks
        # exactly like a visualiser that is switched off, and both look exactly
        # like a rig working normally - so the two states worth naming get
        # named without anyone having to know to open the panel.
        self._osc_quiet_said = False
        self._osc_unbound_said = False
        self._osc_started_at = time.monotonic()

        # Everything this desk sends, seen. --osc's link if there is one; the
        # map's lazily-built one is hooked in _map_osc, because it may not
        # exist until a pad is pressed.
        if self._osc is not None:
            self._osc.on_send = self.osc_traffic.saw_out

        # Every single-key binding steps aside while an Entry has the focus:
        # the mapping editor and the knobs are full of text fields, and a
        # scene name with a q in it must not close the window.
        def hotkey(action):
            def handler(event):
                if isinstance(self.root.focus_get(), tk.Entry):
                    return
                action()
            return handler

        self.root.bind("<space>", hotkey(self._toggle_blackout))
        self.root.bind("<Key-n>", hotkey(lambda: self._step_pattern(1)))
        self.root.bind("<Key-p>", hotkey(lambda: self._step_pattern(-1)))
        self.root.bind("<Up>", hotkey(lambda: self._nudge_master(0.05)))
        self.root.bind("<Down>", hotkey(lambda: self._nudge_master(-0.05)))
        self.root.bind("<Right>", hotkey(lambda: self._nudge_speed(1.25)))
        self.root.bind("<Left>", hotkey(lambda: self._nudge_speed(0.8)))
        self.root.bind("<Key-t>", hotkey(lambda: self._run_button(("beat", ""))))
        self.root.bind("<Key-e>", lambda event: self._on_curves_key())
        self.root.bind("<Key-m>", hotkey(self._toggle_midimap))
        self.root.bind("<Key-o>", hotkey(self._toggle_oscpanel))
        self.root.bind("<Key-q>", hotkey(self._quit))
        self.root.bind("<Escape>", lambda event: self._quit())
        self.root.protocol("WM_DELETE_WINDOW", self._quit)
        self._install_signal_handlers()

        # Panels wait for the executable to say what devices there are - which
        # arrives a moment after it starts. _pump builds them then.

    # -- buttons -----------------------------------------------------------

    def _build_buttons(self) -> None:
        """Lays out the hardcoded button tables.

        Built once. Which rows are *shown* changes with the running pattern —
        state buttons are meaningless on a pattern that has no states — but
        rebuilding widgets on every pattern switch would make them flicker
        under the cursor, so they are packed and unpacked instead.
        """
        # A split, because the two halves answer different questions. The left
        # is the cue list — what is running — and it is a fixed table you learn
        # the shape of. The right is the running look's own knobs, which change
        # every time the left is clicked. Mixing them in one column meant the
        # buttons moved whenever a look with more properties came up.
        #
        # A real sash rather than two packed frames: how much room the knobs
        # want depends on the look, and that is a judgement for whoever is at
        # the desk rather than one to hardcode.
        self.split = tk.PanedWindow(
            self.root, orient="horizontal", bg=PANEL,
            sashwidth=5, sashrelief="flat", borderwidth=0, showhandle=False,
            opaqueresize=True,
        )
        self.split.pack(side="bottom", fill="x")

        self.button_panel = tk.Frame(self.split, bg=PANEL)
        self.param_pane = tk.Frame(self.split, bg=PANEL)
        self.split.add(self.button_panel, stretch="always", minsize=260)
        self.split.add(self.param_pane, stretch="always", minsize=180)

        # -- the cue list, grouped ------------------------------------------
        # Curated groups are built once; generated groups are rebuilt whenever
        # the running machine's state list changes. One containing frame, so
        # the whole cue list packs and unpacks as a unit.
        self.cue_area = tk.Frame(self.button_panel, bg=PANEL)

        self._state_buttons: Dict[str, tk.Button] = {}
        self._curated_groups: List[CueGroup] = []
        self._generated_groups: List[CueGroup] = []
        #: The nest the generated families live in, named for the machine.
        self._generated_section: Optional[CueSection] = None
        self._generated_for: Tuple[str, ...] = ()
        #: collapse decisions made by hand, by group title. An auto default
        #: never overrides one of these.
        self._collapse_override: Dict[str, bool] = {}

        for title, entries in STATE_GROUPS:
            group = CueGroup(self.cue_area, title, on_toggle=self._on_group_toggled)
            for label, command in entries:
                button = group.add_button(label, command, self._run_button)
                if command[0] == "state":
                    self._state_buttons[command[1]] = button
            group.frame.pack(side="left", anchor="n", padx=2)
            self._curated_groups.append(group)

        #: what the curated tables already cover; a generated button exists
        #: only for a state outside this set
        self._curated_states = set(self._state_buttons)

        # the plain patterns, plus the momentary inputs, share a row
        self.extra_row = tk.Frame(self.button_panel, bg=PANEL)
        for label, command in PATTERN_BUTTONS:
            tk.Button(
                self.extra_row, text=label, font=("Consolas", 9),
                bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
                highlightthickness=0, borderwidth=0,
                command=lambda c=command: self._run_button(c),
            ).pack(side="left", padx=3, pady=3)

        tk.Label(self.extra_row, text="   ", bg=PANEL).pack(side="left")

        self._input_buttons: Dict[str, tk.Button] = {}
        for label, channel in INPUT_BUTTONS:
            button = tk.Button(
                self.extra_row, text=label, font=("Consolas", 9),
                bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
                highlightthickness=0, borderwidth=0,
            )
            # momentary: held, not toggled, like the buttons these looks were
            # written around
            button.bind("<ButtonPress-1>", lambda e, c=channel: self._set_input(c, True))
            button.bind("<ButtonRelease-1>", lambda e, c=channel: self._set_input(c, False))
            button.pack(side="left", padx=3, pady=3)
            self._input_buttons[channel] = button

        # the curve editor's toggle, in the band the rest of the desk lives in
        tk.Label(self.extra_row, text="   ", bg=PANEL).pack(side="left")
        self._curves_button = tk.Button(
            self.extra_row, text="curves", font=("Consolas", 9),
            bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
            highlightthickness=0, borderwidth=0,
            command=self._toggle_curves,
        )
        self._curves_button.pack(side="left", padx=3, pady=3)

        # and the midi map's, beside it
        self._midimap_button = tk.Button(
            self.extra_row, text="midi", font=("Consolas", 9),
            bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
            highlightthickness=0, borderwidth=0,
            command=self._toggle_midimap,
        )
        self._midimap_button.pack(side="left", padx=3, pady=3)

        # and the osc watcher's, beside that
        self._oscpanel_button = tk.Button(
            self.extra_row, text="osc", font=("Consolas", 9),
            bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
            highlightthickness=0, borderwidth=0,
            command=self._toggle_oscpanel,
        )
        self._oscpanel_button.pack(side="left", padx=3, pady=3)

        self.extra_row.pack(fill="x")

        # -- the layers, a row each, under the patterns --------------------
        # Empty and unpacked until the executable announces one; see
        # _refresh_layers. A layer is its own small cue list - the UV's
        # off / flash / on - that the show's cues never touch.
        self.layer_row = tk.Frame(self.button_panel, bg=PANEL)

        # -- tempo, division and master, on their own row ------------------
        # Separate from the pattern row because these apply to whatever is
        # running: they are the desk, not the cue list.
        self.tempo_row = tk.Frame(self.button_panel, bg=PANEL)

        tk.Label(self.tempo_row, text="tempo ", bg=PANEL, fg=TEXT_DIM,
                 font=("Consolas", 9)).pack(side="left")

        for label, command in TEMPO_BUTTONS:
            tk.Button(
                self.tempo_row, text=label, font=("Consolas", 9),
                bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
                highlightthickness=0, borderwidth=0,
                command=lambda c=command: self._run_button(c),
            ).pack(side="left", padx=3, pady=3)

        # -- master brightness ---------------------------------------------
        # A slider rather than more buttons: this is the one control that gets
        # ridden continuously rather than set, and it is the one you reach for
        # when the rig is too bright in a room you have not seen before.
        tk.Label(self.tempo_row, text="   master ", bg=PANEL, fg=TEXT_DIM,
                 font=("Consolas", 9)).pack(side="left")

        self._master_var = tk.DoubleVar(value=self._master * 100.0)
        self.master_slider = tk.Scale(
            self.tempo_row,
            from_=0, to=100, resolution=1,
            orient="horizontal", length=180, showvalue=True,
            variable=self._master_var,
            command=self._on_master_slider,
            bg=PANEL, fg=TEXT, troughcolor=BUTTON_BG,
            activebackground=BUTTON_BG_ACTIVE, highlightthickness=0,
            borderwidth=0, sliderrelief="flat", font=("Consolas", 8),
        )
        self.master_slider.pack(side="left", padx=3)

        self.tempo_row.pack(fill="x")

        # -- the relic on the other end of the cable ------------------------
        # Only meaningful on a show driving one, so the row is packed only when
        # the config says so - a dead row of buttons on a DMX rig would be
        # furniture that does nothing.
        self.link_row = tk.Frame(self.button_panel, bg=PANEL)

        tk.Label(self.link_row, text="relic ", bg=PANEL, fg=TEXT_DIM,
                 font=("Consolas", 9)).pack(side="left")

        self._link_buttons: Dict[str, tk.Button] = {}
        for label, command in LINK_BUTTONS:
            button = tk.Button(
                self.link_row, text=label, font=("Consolas", 9),
                bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
                highlightthickness=0, borderwidth=0,
                command=lambda c=command: self._run_button(c),
            )
            button.pack(side="left", padx=3, pady=3)
            self._link_buttons[command[1]] = button

        self.link_note = tk.Label(self.link_row, text="", bg=PANEL, fg=TEXT_DIM,
                                  font=("Consolas", 9))
        self.link_note.pack(side="left", padx=8)

        if self.config.device.type in RELIC_TYPES:
            self.link_row.pack(fill="x")

        # -- the running look's own knobs, in the right pane ----------------
        # Everything on the left is fixed furniture. This panel is not: what it
        # holds comes from whatever is running, over the protocol.
        look_header = tk.Frame(self.param_pane, bg=PANEL)
        look_header.pack(fill="x", pady=(4, 0))
        tk.Label(look_header, text="look", bg=PANEL, fg=TEXT_DIM,
                 font=("Consolas", 9), anchor="w", padx=8).pack(side="left")

        # Whose look: the show's, or a layer's. One button each, the layers'
        # added as the executable announces them. The pane, the curve
        # editor's aim and reset all follow the choice.
        self._knob_target_row = tk.Frame(look_header, bg=PANEL)
        self._knob_target_row.pack(side="left")
        self._add_knob_target("show", self.show)

        # -- kept looks: a dropdown of this state's presets, and a way to
        # add one. The menu applies on pick; "add" snapshots every knob and
        # curve as they stand and asks what to call it.
        self._preset_menu = tk.OptionMenu(look_header, tk.StringVar(), "")
        self._preset_menu.configure(
            # unbind the variable so the button reads as a menu, not a value:
            # picking a preset is an action, and the state it acted on moves
            # on the moment a slider is touched
            textvariable="", text="presets", font=("Consolas", 8),
            bg=BUTTON_BG, fg=TEXT_DIM, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", highlightthickness=0,
            borderwidth=0, indicatoron=False, padx=6, pady=1)
        self._preset_menu["menu"].configure(
            bg=BUTTON_BG, fg=BUTTON_FG, font=("Consolas", 9),
            activebackground=BUTTON_BG_ACTIVE, borderwidth=0)
        self._preset_menu.pack(side="left", padx=(4, 0))

        tk.Button(
            look_header, text="add", font=("Consolas", 8),
            bg=BUTTON_BG, fg=TEXT_DIM, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", padx=6, pady=1,
            highlightthickness=0, borderwidth=0,
            command=self._save_look_preset,
        ).pack(side="left", padx=3)

        # The way back: every knob and curve to the values the cue
        # constructed, from the look's first announcement. What makes tuning
        # and curve edits safe to try.
        tk.Button(
            look_header, text="reset", font=("Consolas", 8),
            bg=BUTTON_BG, fg=TEXT_DIM, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", padx=6, pady=1,
            highlightthickness=0, borderwidth=0,
            command=self._reset_look,
        ).pack(side="right", padx=8)

        self.param_panel = tk.Frame(self.param_pane, bg=PANEL)
        self.param_panel.pack(fill="both", expand=True, padx=4, pady=2)
        # The second half is whatever else the row carries: a box for a float, a
        # swatch button for a colour, nothing for a bool.
        self._param_widgets: Dict[str, Tuple[tk.Variable, Optional[tk.Widget]]] = {}
        self._param_sent: Dict[str, Union[float, str]] = {}
        self._param_revision = -1
        self._param_writing = False
        self._sash_placed = False
        self._split_height = 0

        self._refresh_buttons()

    def _resize_split(self) -> None:
        """Sizes the split to whichever half is taller.

        A PanedWindow keeps whatever height it was given and does not follow its
        panes' requests, and both halves here change height while running — the
        cue rows when a pattern has no states, the knobs on every cue change.
        Without this it holds its first size and clips the taller side, which is
        how the whole control strip ends up below the bottom of the window.
        """
        if self._closing:
            return

        # Both callers have just packed or destroyed widgets, and a requested
        # size is not recomputed until tk goes idle. Measuring without this
        # reads the layout as it was before the change that prompted the call.
        self.split.update_idletasks()

        wanted = max(self.button_panel.winfo_reqheight(), self.param_pane.winfo_reqheight())
        if wanted > 1 and wanted != self._split_height:
            self._split_height = wanted
            self.split.configure(height=wanted)

        # Give the knobs a share of the width the first time the window is real,
        # then never touch the sash again: where it sits after that is a
        # decision someone at the desk has made.
        if not self._sash_placed and self.split.winfo_width() > 1:
            self._sash_placed = True

            # Enough for the cue band on the left and one column of knobs on
            # the right. Both halves have a natural width and they add up to
            # about the window, so this is close to what a drag would land on
            # anyway - it just saves doing it on every launch.
            self.split.sash_place(0, int(self.split.winfo_width() * 0.74), 0)

    # -- the running look's knobs -----------------------------------------

    def _add_knob_target(self, label: str, target) -> None:
        button = tk.Button(
            self._knob_target_row, text=label, font=("Consolas", 8),
            bg=BUTTON_BG, fg=TEXT_DIM, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", padx=6, pady=1,
            highlightthickness=0, borderwidth=0,
            command=lambda t=target: self._select_knobs(t),
        )
        button.pack(side="left", padx=2)
        self._knob_target_buttons[label] = button
        self._refresh_knob_target_buttons()

    def _refresh_knob_target_buttons(self) -> None:
        for label, button in self._knob_target_buttons.items():
            chosen = (self._knobs is self.show and label == "show") or (
                self._knobs is not self.show and getattr(self._knobs, "name", None) == label)
            button.configure(bg=BUTTON_BG_ACTIVE if chosen else BUTTON_BG,
                             fg=BUTTON_FG if chosen else TEXT_DIM)

    def _select_knobs(self, target) -> None:
        """Points the knob pane, the curve editor and reset at a look."""
        if target is self._knobs:
            return
        # an audition on the old look's knob must end before the aim moves
        self.curve_editor.stop_playback()
        self._knobs = target
        self._param_revision = target.params_revision
        self._build_params()
        self._refresh_curve_targets()
        self._refresh_look_presets()
        self._refresh_knob_target_buttons()

    # -- the layers -------------------------------------------------------

    def _refresh_layers(self) -> None:
        """A row per layer: its name, then one button per state.

        Built when the executable announces a layer, which is after the
        first frame. Clicking a state cues that layer alone; the show's
        cues are untouched. Clicking the name points the knob pane at it.
        """
        for name, layer in self.show.layers.items():
            if name in self._layer_buttons:
                continue
            row = tk.Frame(self.layer_row, bg=PANEL)
            row.pack(fill="x")
            tk.Button(
                row, text=name, font=("Consolas", 9, "bold"),
                bg=PANEL, fg=TEXT_DIM, activebackground=PANEL,
                activeforeground=TEXT, relief="flat", padx=8, pady=3,
                highlightthickness=0, borderwidth=0,
                command=lambda t=layer: self._select_knobs(t),
            ).pack(side="left")

            buttons: Dict[str, tk.Button] = {}
            for state in layer.state_names:
                button = tk.Button(
                    row, text=state.replace("_", " "), font=("Consolas", 9),
                    bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                    activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
                    highlightthickness=0, borderwidth=0,
                    command=lambda t=layer, s=state: self._cue_layer(t, s),
                )
                button.pack(side="left", padx=3, pady=3)
                buttons[state] = button
            self._layer_buttons[name] = buttons
            self._add_knob_target(name, layer)

        if self._layer_buttons and not self.layer_row.winfo_ismapped():
            self.layer_row.pack(fill="x", after=self.extra_row)
            self._resize_split()

    def _refresh_layer_buttons(self) -> None:
        for name, buttons in self._layer_buttons.items():
            layer = self.show.layers.get(name)
            if layer is None:
                continue
            for state, button in buttons.items():
                button.configure(bg=BUTTON_BG_ACTIVE if state == layer.current_state else BUTTON_BG)

    def _cue_layer(self, layer, state: str) -> None:
        self._guard(lambda: layer.set_state(state), f"layer {layer.name}")
        self._refresh_layer_buttons()
        # the layer's knobs changed with its look; if the pane is on it,
        # follow - the revision check in the pump does the rebuild

    def _build_params(self) -> None:
        """Builds a control per property the running look offers.

        Rebuilt from scratch whenever the set changes, unlike the state buttons
        which are packed and unpacked: *which* knobs exist is a property of the
        look, so there is no stable set of widgets to keep around. A cue change
        replaces the panel.

        A float gets a slider and a box. The slider is for finding a value and
        the box is for saying one - a 0..3 slider 110 pixels wide cannot express
        0.15, and an envelope tuned to the nearest pixel is not tuned. A bool
        gets a checkbox, and a colour gets a swatch that opens the system
        picker: a colour is chosen by looking at it, never by typing three
        numbers at it.
        """
        for child in self.param_panel.winfo_children():
            child.destroy()
        self._param_widgets = {}
        self._param_sent = {}

        params = list(self._knobs.params)
        if not params:
            # Said out loud rather than left blank. An empty pane reads as a
            # panel that failed to load; this reads as a look with no knobs,
            # which is a real and common answer.
            tk.Label(self.param_panel, text="nothing to tune", bg=PANEL, fg=TEXT_DIM,
                     font=("Consolas", 9)).grid(row=0, column=0, sticky="w", padx=6, pady=2)
            self._resize_split()
            return

        # One knob per row. Two columns fit in the pane only until a look turns
        # up with a long name or a rig is run on a narrower window, and a
        # control that is half off the edge is worse than one further down.
        columns = 1
        for index, param in enumerate(params):
            cell = tk.Frame(self.param_panel, bg=PANEL)
            cell.grid(row=index // columns, column=index % columns, sticky="w", padx=6, pady=1)

            if param.is_bool:
                variable: tk.Variable = tk.IntVar(value=1 if param.value else 0)
                tk.Checkbutton(
                    cell, text=param.name, variable=variable,
                    bg=PANEL, fg=TEXT, selectcolor=BUTTON_BG,
                    activebackground=PANEL, activeforeground=TEXT,
                    font=("Consolas", 9), highlightthickness=0, borderwidth=0,
                    command=lambda name=param.name, v=variable: self._apply_param(name, float(v.get())),
                ).pack(side="left")
                self._param_widgets[param.name] = (variable, None)
                continue

            # Fixed-width label so the sliders line up down the column. A name
            # longer than this overflows into its own slider rather than
            # shunting the row sideways out of the pane.
            tk.Label(cell, text=param.name, bg=PANEL, fg=TEXT_DIM, width=14,
                     anchor="w", font=("Consolas", 9)).pack(side="left")

            if param.is_color:
                # The swatch *is* the control: it shows the colour and clicking
                # it changes the colour. A hex box beside it would be a second
                # way to say the same thing, and one of them would always be
                # stale by a frame.
                variable = tk.StringVar(value=str(param.value))
                swatch = tk.Button(
                    cell, width=10, text="", relief="flat", borderwidth=0,
                    highlightthickness=1, highlightbackground=BUTTON_BG,
                    bg=str(param.value), activebackground=str(param.value),
                    command=lambda name=param.name: self._pick_color(name),
                )
                swatch.pack(side="left", padx=(4, 2))

                self._param_widgets[param.name] = (variable, swatch)
                self._param_sent[param.name] = param.value
                continue

            variable = tk.DoubleVar(value=param.value)
            tk.Scale(
                cell,
                from_=param.minimum, to=param.maximum,
                resolution=_slider_step(param.minimum, param.maximum),
                orient="horizontal", length=88, showvalue=False,
                variable=variable,
                command=lambda value, name=param.name: self._on_param_slider(name, value),
                bg=PANEL, fg=TEXT, troughcolor=BUTTON_BG,
                activebackground=BUTTON_BG_ACTIVE, highlightthickness=0,
                borderwidth=0, sliderrelief="flat", font=("Consolas", 8),
            ).pack(side="left", padx=(4, 2))

            entry = tk.Entry(
                cell, width=5, justify="right",
                bg=BUTTON_BG, fg=TEXT, insertbackground=TEXT,
                font=("Consolas", 9), relief="flat", highlightthickness=0,
            )
            entry.insert(0, f"{param.value:g}")
            entry.bind("<Return>", lambda event, name=param.name: self._on_param_entry(name))
            entry.bind("<FocusOut>", lambda event, name=param.name: self._on_param_entry(name))
            entry.pack(side="left")

            self._param_widgets[param.name] = (variable, entry)
            self._param_sent[param.name] = param.value

        self._resize_split()

    def _on_param_slider(self, name: str, value: str) -> None:
        try:
            target = float(value)
        except ValueError:
            return

        # tk fires this on every step of a drag, and _apply_param writes the
        # clamped value back into the same variable, so without a dead-band the
        # two bounce off each other. Same reason as the master slider.
        if self._param_writing:
            return
        if abs(target - self._param_sent.get(name, float("nan"))) < 1e-9:
            return

        self._apply_param(name, target)

    def _on_param_entry(self, name: str) -> None:
        widgets = self._param_widgets.get(name)
        if widgets is None or widgets[1] is None:
            return
        try:
            target = float(widgets[1].get())
        except ValueError:
            # Put the real value back rather than arguing about it.
            self._show_param(name)
            return

        self._apply_param(name, target)

    def _pick_color(self, name: str) -> None:
        """Opens the system colour picker on a colour knob.

        The picker is modal and the show keeps running behind it, which is the
        right way round: you pick against the rig doing what it is doing, not
        against a frozen window.
        """
        param = self._knobs.get_param(name)
        current = str(param.value) if param is not None else "#ffffff"

        try:
            chosen = colorchooser.askcolor(color=current, title=name, parent=self.root)[1]
        except tk.TclError:
            # A colour the picker will not open on - it has been asked to start
            # from something it cannot parse. Fall back rather than take the
            # window down with it.
            chosen = colorchooser.askcolor(title=name, parent=self.root)[1]

        if chosen:
            self._apply_param(name, str(chosen))

    def _apply_param(self, name: str, value: Union[float, str]) -> None:
        self._param_sent[name] = value
        self._guard(lambda: self._knobs.set_param(name, value), f"param {name}")

        # The executable clamps to the range, snaps a rate to a musical one, and
        # echoes what it landed on - so the widgets follow the look rather than
        # the other way round.
        self._show_param(name)

        # A knob can rebuild a curve (attack and decay rewrite the envelope),
        # and the echo re-announces the shapes - so the editor's drawing
        # follows the look too.
        self.curve_editor.refresh_live()

    def _show_param(self, name: str) -> None:
        param = self._knobs.get_param(name)
        widgets = self._param_widgets.get(name)
        if param is None or widgets is None:
            return

        variable, control = widgets
        self._param_writing = True
        try:
            if param.is_color:
                variable.set(str(param.value))
                if control is not None:
                    control.configure(bg=str(param.value), activebackground=str(param.value))
            else:
                variable.set(
                    1 if (param.is_bool and param.value) else (0 if param.is_bool else param.value))
                if control is not None:
                    control.delete(0, "end")
                    control.insert(0, f"{param.value:g}")
        finally:
            self._param_writing = False
        self._param_sent[name] = param.value

    def _refresh_buttons(self) -> None:
        """The cue list follows the running machine.

        Generated groups are rebuilt when the state list changes; curated ones
        are permanent furniture. A group with nothing the machine knows starts
        folded and sinks below the live ones; a group toggled by hand stays
        where its collapse was put.
        """
        state_names = tuple(self.show.state_names)
        has_states = bool(state_names)

        if has_states and not self.cue_area.winfo_ismapped():
            self.cue_area.pack(fill="x", before=self.extra_row)
        elif not has_states and self.cue_area.winfo_ismapped():
            self.cue_area.pack_forget()

        self._rebuild_generated_groups(state_names)

        # a state the running machine does not offer is dimmed, not hidden:
        # the table is yours, and silently dropping an entry would read as a bug
        for name, button in self._state_buttons.items():
            known = name in self.show.state_names
            active = known and name == self.show.current_state
            button.configure(
                bg=BUTTON_BG_ACTIVE if active else BUTTON_BG,
                fg=BUTTON_FG if known else TEXT_DIM,
            )

        # fold what tonight's machine cannot use, open what it can - unless a
        # hand already decided
        for group in self._curated_groups + self._generated_groups:
            if group.title in self._collapse_override:
                group.set_collapsed(self._collapse_override[group.title])
            else:
                group.set_collapsed(not self._group_is_live(group))

        # the band's top level: the machine's section and the curated groups,
        # live ones floating left of the folded. The families inside the
        # section keep their build order - the nest is the unit that moves.
        units: List[Tuple[bool, object]] = []
        if self._generated_section is not None:
            section = self._generated_section
            live = any(self._group_is_live(group) for group in section.groups)
            if section.title in self._collapse_override:
                section.set_collapsed(self._collapse_override[section.title])
            else:
                section.set_collapsed(not live)
            units.append((live, section))
        for group in self._curated_groups:
            units.append((self._group_is_live(group), group))

        for live, unit in sorted(units, key=lambda entry: not entry[0]):
            unit.frame.pack_forget()
            unit.frame.pack(side="left", anchor="n", padx=2)

        self._resize_split()

    def _group_is_live(self, group: CueGroup) -> bool:
        return any(name in self.show.state_names for name in group.states)

    def _on_group_toggled(self, group: CueGroup) -> None:
        self._collapse_override[group.title] = group.collapsed
        self._resize_split()

    def _rebuild_generated_groups(self, state_names: Tuple[str, ...]) -> None:
        """A button for every state no curated table covers.

        Grouped by GENERATED_STATE_GROUPS, in that table's order, with the
        machine's own order kept inside a group. Torn down and rebuilt only
        when the uncovered set changes, which is a pattern switch - never on a
        click, so buttons do not flicker under the cursor.
        """
        wanted = tuple(name for name in state_names if name not in self._curated_states)
        if wanted == self._generated_for:
            return
        self._generated_for = wanted

        for group in self._generated_groups:
            for name in group.states:
                self._state_buttons.pop(name, None)
        self._generated_groups = []
        if self._generated_section is not None:
            self._generated_section.destroy()
            self._generated_section = None

        if not wanted:
            return

        grouped: Dict[str, List[str]] = {}
        for name in wanted:
            grouped.setdefault(_generated_group(name), []).append(name)

        order: List[str] = []
        for title, _prefix in GENERATED_STATE_GROUPS:
            if title in grouped and title not in order:
                order.append(title)
        if "other" in grouped:
            order.append("other")

        # The nest: one section named for the machine, its families as columns
        # inside. The border is what keeps this machine's cues reading as one
        # block rather than bleeding into whatever shares the band.
        section = CueSection(self.cue_area, self.current_pattern,
                             on_toggle=self._on_group_toggled)
        self._generated_section = section

        for title in order:
            group = CueGroup(section.body, title, on_toggle=self._on_group_toggled)
            for name in grouped[title]:
                self._state_buttons[name] = group.add_button(
                    _generated_label(name, title), ("state", name), self._run_button)
            group.frame.pack(side="left", anchor="n", padx=2)
            section.groups.append(group)
            self._generated_groups.append(group)

        section._refresh_header()
        section.frame.pack(side="left", anchor="n", padx=2)

    def _run_button(self, command: Tuple[str, str]) -> None:
        kind, value = command

        def apply() -> None:
            if kind == "state":
                self.show.set_state(value)
                self._fire_cue(value)
            elif kind == "pattern":
                self.show.set_pattern(value)
                self.current_pattern = value
            elif kind == "beat":
                self.show.tap_beat()
            elif kind == "align":
                self.show.midi_align()
            elif kind == "bpm":
                self.show.set_bpm(float(value))
            elif kind == "link":
                if value == "release":
                    self.show.release_link()
                else:
                    self.show.set_link_mode(value)

        self._guard(apply, kind)
        self._refresh_buttons()
        self._refresh_link_buttons()

    def _fire_cue(self, state: str) -> None:
        """The rest of the cue around `state`, if the config has one.

        The state itself has already been set. What follows is what the
        config's cue table puts with it - the layers, the visualiser's scene
        and media - run through the same action registry the pads use, so a
        button here and a pad on the surface do the same thing. Only for the
        machine the config is written for: the same state name on another
        machine is a coincidence, not a cue.

        Each action is fenced on its own, as the dispatcher fences a pad's: a
        visualiser that is not there must not cost the rig its layers.
        """
        if self.current_pattern != self.config.pattern.name:
            return
        if state not in self.config.cues:
            return
        context = self._dispatcher.context
        for entry in cue_actions(self.config, state):
            action = Action.from_dict(entry)
            if action.key in ("pattern", "state"):
                continue    # already done, by the caller
            spec = action.spec
            if spec is None:
                continue
            try:
                line = spec.run(context, action.params, 1.0)
            except Exception as error:  # fenced on purpose; see the docstring
                self._say(f"cue {state}: {error}")
                continue
            if line:
                self._say(line)

    def _refresh_link_buttons(self) -> None:
        """Lights the mode the relic is in, and shows the last thing it said.

        `release` is never lit: it is an action, not a mode - the sculpture goes
        back to its own look and this end stays in pixel mode, ready to take it
        again on the next frame.
        """
        if self.config.device.type not in RELIC_TYPES:
            return

        for value, button in self._link_buttons.items():
            active = (value == self.show.link_mode)
            button.configure(bg=BUTTON_BG_ACTIVE if active else BUTTON_BG)

        said = self.show.relic_lines[-1] if self.show.relic_lines else ""
        self.link_note.configure(text=said[:60])

    def _set_input(self, channel: str, down: bool) -> None:
        if not self.show.state_names:
            return
        self._guard(lambda: self.show.set_input(channel, down), "input")
        button = self._input_buttons.get(channel)
        if button is not None:
            button.configure(bg=BUTTON_BG_ACTIVE if down else BUTTON_BG)

    # -- the midi map ------------------------------------------------------

    def _toggle_midimap(self) -> None:
        """Folds the mapping editor in and out, down the right of the desk."""
        self._midimap_shown = not self._midimap_shown
        if self._midimap_shown:
            # before the surface, so the panel takes the right edge and the
            # desk keeps the rest; the surface is the only thing that flexes
            self.midi_panel.frame.pack(side="right", fill="y",
                                       before=self.surface, padx=(0, 4), pady=2)
        else:
            self.midi_panel.frame.pack_forget()
        self._midimap_button.configure(
            bg=BUTTON_BG_ACTIVE if self._midimap_shown else BUTTON_BG)

    # -- the osc watcher ----------------------------------------------------

    def _toggle_oscpanel(self) -> None:
        """Folds the OSC watcher in and out, down the right of the desk."""
        self._oscpanel_shown = not self._oscpanel_shown
        if self._oscpanel_shown:
            self.osc_panel.frame.pack(side="right", fill="y",
                                      before=self.surface, padx=(0, 4), pady=2)
            self.osc_panel.refresh()
        else:
            self.osc_panel.frame.pack_forget()
        self._oscpanel_button.configure(
            bg=BUTTON_BG_ACTIVE if self._oscpanel_shown else BUTTON_BG)

    def _check_osc_is_landing(self) -> None:
        """Says the two things that are otherwise indistinguishable from fine.

        Both are one-shot and both name the panel, because the panel is where
        the answer is and neither line has room for it.
        """
        if self._osc_listener is None:
            return
        if self._osc_quiet_said and self._osc_unbound_said:
            return

        waited = time.monotonic() - self._osc_started_at
        if waited < OSC_SILENCE_GRACE:
            return

        state = self.osc_traffic.snapshot()

        # Nothing at all. The app's OSC *output* is a separate setting from its
        # input, and a rig whose colour is already reaching the visualiser
        # proves only that the other direction works - which is exactly the
        # evidence that makes this one look like it must be fine too.
        if state["last_in_at"] is None:
            if not self._osc_quiet_said:
                self._osc_quiet_said = True
                from .cli import _my_addresses
                mine = " or ".join(_my_addresses()) or "this machine"
                self._say(f"osc in: nothing on {self._osc_listener.port} in "
                          f"{waited:.0f}s - is Synesthesia's OSC *output* on, and its "
                          f"output IP {mine}? (127.0.0.1 there means the mac, not "
                          f"this desk.) [o] for the panel")
            return
        self._osc_quiet_said = True

        # Traffic, but nothing the map wants. This is the address-mismatch
        # case, and it is the one worth catching: the uniforms are
        # undocumented and were renamed once already, so a shipped map is a
        # set of guesses until something checks them against this build.
        if self._osc_unbound_said:
            return
        if any(self._bindings_for_address(address) for address, _c, _v, _at in state["in"]):
            self._osc_unbound_said = True
            return
        self._osc_unbound_said = True
        self._say(f"osc in: {len(state['in'])} addresses arriving and no binding takes "
                  "any of them - the map's globs do not match this build. [o] for the panel")

    def _describe_osc_out(self) -> str:
        if self._osc is None:
            return "(nothing sending; --osc names a visualiser)"
        return self._osc.describe()

    def _osc_out_stats(self):
        if self._osc is None:
            return (0, 0, None)
        return (self._osc.sent, self._osc.dropped, self._osc.last_error)

    def _bindings_for_address(self, address: str):
        """Which enabled bindings would take this address, by what they drive.

        The whole question the panel exists to answer, and the reason it shows
        the map's verdict beside each arrival rather than just the arrival: a
        glob checked by eye against forty addresses is where this goes wrong.
        """
        if self._osc_dispatcher is None:
            return []
        targets = []
        for binding in self._osc_dispatcher.bindings.bindings:
            if not binding.enabled or not binding.matches(address):
                continue
            if binding.action == "audio":
                targets.append(str(binding.params.get("channel", "audio ?")))
            else:
                named = binding.params.get("name")
                targets.append(f"{binding.action} {named}" if named else binding.action)
        return targets

    def _open_midi(self) -> None:
        """The panel's way in when the show was started without --midi."""
        self._guard(lambda: self.show.midi_open("auto"), "midi open")

    def _on_midi_line(self, payload: str) -> None:
        """Called on the reader thread. Parses and queues; does not touch tk
        and does not fire actions - see the deque's note in __init__."""
        event = parse_midi_line(payload)
        if event is not None:
            self._midi_events.append(event)

    def _drain_midi(self) -> None:
        """Every queued event, through learn and find first and then the
        mappings. On the tk thread, from _pump."""
        last_event = None
        fired: List[str] = []
        while self._midi_events:
            event = self._midi_events.popleft()
            last_event = event
            # Either of the panel's two armed modes takes the event whole:
            # a pad being learned must not also fire, and neither must one
            # being asked about.
            if self.midi_panel.take_learn(event) or self.midi_panel.take_find(event):
                continue
            fired.extend(self._dispatcher.handle(event))
        if last_event is not None:
            self.midi_panel.show_traffic(last_event, fired)
        if fired:
            self._say(fired[-1])

        # Only while it is open. The traffic is recorded either way - see the
        # note where OscTraffic is built - but rewriting a folded-away panel
        # sixty times a second is work nobody can see.
        if self._oscpanel_shown:
            self.osc_panel.refresh()

        self._check_osc_is_landing()

    # -- the curve editor --------------------------------------------------

    def _toggle_curves(self) -> None:
        """Folds the curve editor in and out, between the desk and the buttons."""
        self._curves_shown = not self._curves_shown
        if self._curves_shown:
            # packed bottom-up like the rest of the furniture: the band lands
            # above the split, and the window grows by the band rather than
            # the desk shrinking by it - the rig picture is what a curve is
            # being shaped against, so it is the one thing the band must not
            # eat. A maximised window has nowhere to grow, so there the desk
            # pays after all.
            self.curve_editor.frame.pack(side="bottom", fill="x", padx=4, pady=(0, 4))
            self._refresh_curve_targets()
            self._grow_for_curves(1)
        else:
            # a hidden editor driving a knob would be a haunting, not a tool
            self.curve_editor.stop_playback()
            self.curve_editor.frame.pack_forget()
            self._grow_for_curves(-1)
        self._curves_button.configure(
            bg=BUTTON_BG_ACTIVE if self._curves_shown else BUTTON_BG)

    def _grow_for_curves(self, direction: int) -> None:
        if self.root.state() == "zoomed":
            return
        self.root.update_idletasks()
        band = self.curve_editor.frame.winfo_reqheight() + 4
        self.root.geometry(
            f"{self.root.winfo_width()}x{self.root.winfo_height() + direction * band}")

    def _on_curves_key(self) -> None:
        # [e] lands in the entries too - a value being typed into a param box
        # must not fold the window about underneath it
        if isinstance(self.root.focus_get(), tk.Entry):
            return
        self._toggle_curves()

    def _refresh_curve_targets(self) -> None:
        """Hands the editor the look's float knobs and its live shapes."""
        self.curve_editor.set_targets(
            [
                (param.name, param.minimum, param.maximum)
                for param in self._knobs.params
                if not param.is_bool and not param.is_color
            ],
            curve_names=list(self._knobs.curves.keys()),
        )

    def _push_look_curve(self, name: str, keys) -> None:
        """One edited shape, into the running look's own curve."""
        self._guard(lambda: self._knobs.set_curve(name, keys), f"curve {name}")

    def _look_curve_keys(self, name: str):
        return self._knobs.curves.get(name)

    def _reset_look(self) -> None:
        """Every knob and curve back to what the cue constructed."""
        # a running audition would re-stomp the freshly reset knob on its
        # next tick, and then "restore" a pre-reset value on stop
        self.curve_editor.stop_playback()

        self._guard(self._knobs.reset_look, "reset")

        # the echoes have landed (reset_look sends synchronously): put every
        # widget and the drawn shape back in step with the look
        for name in list(self._param_widgets):
            self._show_param(name)
        self.curve_editor.refresh_live()

    # -- look presets ------------------------------------------------------

    def _look_state(self) -> str:
        """Which folder a preset belongs to: the state, or for a plain
        pattern with no states, the pattern itself."""
        if self._knobs is not self.show:
            # a layer's presets file under the layer's own state, in the
            # layer's own folder, so `on` for the UV never meets the show's
            return f"{self._knobs.name}.{self._knobs.current_state or 'look'}"
        return self.show.current_state or self.current_pattern

    def _refresh_look_presets(self) -> None:
        """Rebuilds the dropdown for the current state. Compared against a
        signature because the pump calls this on every params revision, and
        most revisions are a slider echo, not a new cue."""
        state = self._look_state()
        names = look_presets.list_presets(self._preset_dir, state)
        signature = (state, tuple(names))
        if signature == self._preset_signature:
            return
        self._preset_signature = signature

        menu = self._preset_menu["menu"]
        menu.delete(0, "end")
        if not names:
            menu.add_command(label="(no presets kept)", state="disabled")
            return
        for name in names:
            menu.add_command(label=name,
                             command=lambda n=name: self._apply_look_preset(n))

    def _apply_look_preset(self, name: str) -> None:
        state = self._look_state()
        try:
            preset = look_presets.load_preset(
                look_presets.preset_path(self._preset_dir, state, name))
        except (OSError, ValueError, KeyError) as error:
            self._say(f"preset {name}: {error}")
            return

        # not through _guard: success clears the status there, and the
        # skipped-names report is this feature's whole answer to a preset
        # tried on a look that half-fits
        try:
            applied, skipped = look_presets.apply_preset(self.show, preset)
        except ShowError as error:
            self._say(f"preset {name}: {error}")
            return
        note = f"preset {name}: {applied} set"
        if skipped:
            note += f", no home for {', '.join(skipped[:4])}"
            if len(skipped) > 4:
                note += f" +{len(skipped) - 4}"
        self._say(note)

    def _save_look_preset(self) -> None:
        """Snapshot the running look's values, ask for a name, keep it."""
        if not self._knobs.params and not self._knobs.curves:
            self._say("preset: this look has no knobs to keep")
            return
        name = simpledialog.askstring(
            "keep this look", "name this preset:", parent=self.root)
        if not name or not name.strip():
            return
        name = name.strip()

        # _look_state, not current_state: a stateless pattern's presets file
        # under the pattern's own name rather than all sharing one folder
        preset = look_presets.snapshot(
            self._knobs, name, self.current_pattern, self._look_state())
        try:
            path = look_presets.save_preset(self._preset_dir, preset)
        except OSError as error:
            self._say(f"preset {name}: {error}")
            return
        # forget the signature so the new file shows up this pump
        self._preset_signature = ()
        self._refresh_look_presets()
        self._say(f"kept {path.parent.name}/{path.name}")

    def _hold_target(self, name: str, active: bool) -> None:
        """The editor's transport borrows a knob; this is the lease.

        On begin, the knob's current value is held; on end it goes back -
        playing a curve at the intensity is an audition, not an edit. The
        restore targets the *held* name, so an aim moved mid-session still
        returns the right knob.
        """
        if active:
            if name == "master":
                self._held_target = ("master", self._master)
            else:
                param = self._knobs.get_param(name)
                self._held_target = (name, param.value) if param else None
            return

        held = self._held_target
        self._held_target = None
        if held is None:
            return

        held_name, value = held
        if held_name == "master":
            self._set_master(value)
        else:
            self._guard(lambda: self._knobs.set_param(held_name, value),
                        f"restore {held_name}")
            self._show_param(held_name)

    def _animate_target(self, name: str, value: float) -> None:
        """One curve sample, onto whatever the editor is aimed at."""
        if name == "master":
            self._set_master(value)
            return
        self._guard(lambda: self._knobs.set_param(name, value), f"curve {name}")
        # the executable's echo drives the knob's own slider, so the panel
        # visibly plays the curve too
        self._show_param(name)

    def _say(self, message: str) -> None:
        self._status = message
        self._refresh_header()

    def _rebuild_items(self) -> None:
        """Builds one panel per device and tiles them across the surface.

        Called once the executable has announced its devices, and again if that
        set ever changes. Not on every resize: panels are dragged and sized by
        hand, and re-tiling them under the cursor because the window moved would
        undo that.
        """
        spans = list(self.show.devices)
        signature = tuple((span.name, span.first, span.count) for span in spans)
        if signature == self._panel_signature and self._panels:
            return
        self._panel_signature = signature

        for panel in self._panels:
            panel.destroy()
        self._panels = []

        if not spans:
            return

        # Fixtures come out of the config rather than off the wire, because the
        # config is what knows where they physically are. The DEVICE lines say
        # which slice of a frame is whose; the config says what that slice looks
        # like.
        # Which of them came up without a wire. Known only now: the config says
        # what is in the room, and only a started show says what is plugged in.
        offline = dict(self.show.offline_devices)
        unknown: List[str] = []

        for span in spans:
            device = self.config.device_named(span.name)
            if device is None and len(self.config.devices) == len(spans):
                device = self.config.devices[span.index]
            if device is None:
                # On the rig, absent from this config - which over ssh means
                # the two checkouts disagree about the show. Said rather than
                # skipped silently: a panel that is simply not there is a
                # thing you notice and cannot explain.
                unknown.append(span.name)
                continue

            panel = DevicePanel(
                self.surface, span.name, plan_layout(device),
                device=device, on_touched=self._mark_panels_touched)
            # The span this panel draws, kept *on* the panel. A device the
            # config has never heard of is skipped just above, so the panels
            # and the spans are not parallel lists and an index into one is
            # not an index into the other - which over ssh, against a rig with
            # a device this config lacks, drew every panel one device early.
            panel.span = span
            if span.name in offline:
                panel.set_offline(offline[span.name])
            self._panels.append(panel)

        if unknown:
            # Over ssh this is the two checkouts disagreeing about the show,
            # and it is worth saying out loud: the rig is rendering a device
            # this config has no positions for, so it cannot be drawn - and
            # the frame it occupies is real, which is what used to shift every
            # other panel along.
            self._say("rig has " + ", ".join(unknown)
                      + ": not in this config, not drawn")

        # A focus on a device that is no longer in the show is a focus on
        # nothing - back to the desk.
        if self._focus_device not in {panel.name for panel in self._panels}:
            self._focus_device = None

        self._rebuild_device_row()
        self._apply_layout()

    def _rebuild_device_row(self) -> None:
        for button in self._device_buttons.values():
            button.destroy()
        self._device_buttons = {}

        if len(self._panels) < 2:
            self.device_row.pack_forget()
            return

        if not self.device_row.winfo_ismapped():
            self.device_row.pack(fill="x", after=self.header)

        names: List[Optional[str]] = [None] + [panel.name for panel in self._panels]
        for name in names:
            button = tk.Button(
                self.device_row, text="all" if name is None else name,
                font=("Consolas", 9),
                bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                activeforeground=BUTTON_FG, relief="flat", padx=8, pady=2,
                highlightthickness=0, borderwidth=0,
                command=lambda n=name: self._focus_on(n),
            )
            button.pack(side="left", padx=3, pady=3)
            self._device_buttons[name] = button

        self._refresh_device_buttons()

    def _refresh_device_buttons(self) -> None:
        for name, button in self._device_buttons.items():
            button.configure(bg=BUTTON_BG_ACTIVE if name == self._focus_device else BUTTON_BG)

    def _focus_on(self, name: Optional[str]) -> None:
        if name == self._focus_device:
            return
        self._focus_device = name
        self._refresh_device_buttons()
        self._apply_layout()

    def _apply_layout(self) -> None:
        """The surface, laid out for the current focus.

        "all" is the tiled desk, dragged and sized however it was left. A
        focused device takes the whole surface and the rest step out of the
        way - they keep painting, so flipping through devices is instant and
        the picture is always current when it lands.

        Coming back to "all" re-tiles rather than restoring hand positions:
        the coordinates a drag left behind may belong to a window size that no
        longer exists, and a fresh even tile is legible where a half-restored
        layout is a mess.
        """
        if not self._panels:
            return

        if self._focus_device is None:
            self._panels_touched = False
            self._tile_panels()
            return

        gap = 6
        width = max(self.surface.winfo_width() - 2 * gap, 120)
        height = max(self.surface.winfo_height() - 2 * gap, 90)
        for panel in self._panels:
            if panel.name == self._focus_device:
                panel.place(x=gap, y=gap, width=width, height=height)
                panel.frame.lift()
            else:
                panel.frame.place_forget()

    def _tile_panels(self) -> None:
        """An opening arrangement: side by side, widest device first.

        Only ever the starting point. Everything after this is whatever the
        panels were dragged to.
        """
        if not self._panels:
            return

        width = max(self.surface.winfo_width(), 1)
        height = max(self.surface.winfo_height(), 1)
        if width <= 1 or height <= 1:
            return

        # An even split, scaled per device by whatever its config asked for.
        #
        # Not inferred from the geometry: a pillar and a truss want different
        # panel shapes, but *how* different is a judgement about the room and
        # the screen, not something derivable from a fixture list. It is one
        # vector in the environment - see Placement.view_scale - so it can be
        # tuned by looking at the window rather than by rewriting a heuristic.
        gap = 6
        count = len(self._panels)
        room = width - gap * (count + 1)
        full_height = max(height - 2 * gap, 80)

        scales = [panel.view_scale for panel in self._panels]
        total = sum(scale[0] for scale in scales) or 1.0

        x = gap
        for panel, scale in zip(self._panels, scales):
            panel_width = max(int(room * scale[0] / total), self.MIN_PANEL)
            panel_height = max(int(full_height * scale[1]), 90)
            panel.place(x=x, y=gap, width=panel_width, height=panel_height)
            x += panel_width + gap

    def _mark_panels_touched(self) -> None:
        self._panels_touched = True

    def _on_surface_resized(self) -> None:
        """Re-fit a focused panel; re-tile the desk only while it is untouched."""
        if self._focus_device is not None:
            self._apply_layout()
        elif not self._panels_touched:
            self._tile_panels()

    # -- the show ----------------------------------------------------------

    def _on_frame(self, frame: Frame) -> None:
        """Called on the reader thread. Does not touch tk."""
        with self._lock:
            self._latest = frame
            self._frames_seen += 1

        if self._osc is not None:
            self._send_osc(frame)

        # And on to the wires, when they are somebody else's. Same thread and
        # the same frame the OSC sender gets: this is one picture going to
        # three places - the screen, the visualiser, and the rig.
        if self._sink is not None:
            self._send_to_sink(frame)

    def _send_osc(self, frame: Frame) -> None:
        """One fixture of this frame, out to the visualiser.

        On the reader thread, and deliberately not handed to the tk loop first:
        the window redraws at whatever rate it manages and the link should not
        inherit that. Touches no widget, for the same reason.
        """
        if not self._osc_resolved:
            self._osc_resolved = True
            if self._osc_device:
                span = next((d for d in self.show.devices
                             if d.name == self._osc_device), None)
                if span is not None:
                    self._osc_fixture = span.first

        if self._osc_fixture < len(frame):
            self._osc.send_color(frame[self._osc_fixture])

    def _current_frame(self) -> Optional[Frame]:
        with self._lock:
            return self._latest

    def _pump(self) -> None:
        """The tk-side loop: drain the newest frame, redraw, measure."""
        self._pump_id = None

        if self._closing:
            return

        # A Ctrl-C or a kill, noticed here rather than in the handler that
        # took it - see _install_signal_handlers.
        if self._bail_requested:
            self._quit()
            return

        if not self.show.is_running:
            self._status = "the show has ended"
            self._refresh_header()
            self._paint(None)
            return

        with self._lock:
            frame = self._latest
            seen = self._frames_seen

        # The DEVICE lines arrive on the reader thread a moment after start, so
        # the panels are built from here rather than in __init__. Cheap once
        # they exist: it compares a signature and returns.
        self._rebuild_items()

        self._paint(frame)

        # Pad presses queued by the reader thread, fired here where talking
        # the protocol back is safe. Before the button refresh below, so a
        # pad that changed the state lights the matching cue button in the
        # same frame.
        self._drain_midi()

        # STATES and STATE arrive on the reader thread, whenever the executable
        # gets round to them, so the buttons follow from here rather than from
        # the click that caused it.
        signature = (tuple(self.show.state_names), self.show.current_state)
        if signature != self._button_signature:
            self._button_signature = signature
            self._refresh_buttons()
            self._refresh_header()

        # The lamps follow the same truth as the cue buttons, from the same
        # place - so a state changed by a pad, by a click, by a shortcut or by
        # an OSC binding all move the surface. Called every tick rather than
        # only on the signature above, because the map itself changes under
        # the editor too; the painter diffs, so an unchanged surface is free.
        self._paint_lamps()

        # A relic says when a takeover starts or lapses, and the note beside the
        # link buttons is where that belongs - the alternative is watching a
        # sculpture to find out whether it is still listening.
        relic_said = len(self.show.relic_lines)
        if relic_said != self._relic_said:
            self._relic_said = relic_said
            self._refresh_link_buttons()

        # Same reason, for the knobs: the executable announces a new set on
        # every pattern and state change, and the revision is what says so.
        if self._knobs.params_revision != self._param_revision:
            self._param_revision = self._knobs.params_revision
            self._build_params()
            self._refresh_curve_targets()
            self._refresh_look_presets()

        # The layers: a row each when they are announced, and their state
        # buttons following the executable's word on which is showing.
        if self.show.layers_revision != self._layers_revision:
            self._layers_revision = self.show.layers_revision
            self._refresh_layers()
        self._refresh_layer_buttons()

        now = time.monotonic()
        elapsed = now - self._fps_marker
        if elapsed >= 0.5:
            self._fps = (seen - self._fps_counted) / elapsed
            self._fps_counted = seen
            self._fps_marker = now
            self._refresh_header()

        self._pump_id = self.root.after(16, self._pump)

    def _paint(self, frame: Optional[Frame], force: bool = False) -> None:
        """Hands each panel its own slice of the frame.

        A frame is one flat run across every device in order; the DEVICE lines
        said where each run starts. Slicing here rather than in the panels keeps
        the panels ignorant of the environment they are in.
        """
        if not force and frame is self._painted:
            return
        self._painted = frame

        # the double-gamma fix - see __init__. On the raw frame once, so every
        # panel and slice below shares the one corrected copy.
        shown = frame
        if shown is not None and self._ungamma is not None:
            table = self._ungamma
            shown = [(table[r], table[g], table[b]) for r, g, b in shown]

        for panel in self._panels:
            span = getattr(panel, "span", None)
            if shown is None:
                panel.paint(None)
            elif span is not None:
                panel.paint(span.slice(shown))
            else:
                # No span means no DEVICE line claimed this panel; drawing the
                # whole frame is the old behaviour and still the best guess.
                panel.paint(shown)


    def _refresh_header(self) -> None:
        source = "LIVE" if self.live else "dry-run"
        if self.host:
            source += f" on {self.host}"
        parts = [f"{self.current_pattern}"]
        if self.show.current_state and self.show.state_names:
            parts.append(self.show.current_state)
        parts += [
            f"{len(self.placements)} fixtures",
            f"{self._fps:4.1f} fps",
        ]

        # Only once a beat line has actually arrived. Showing "0.0 bpm" before
        # the first one would read as a tempo of zero rather than as no news.
        if self.show.bpm > 0.0:
            lock = "lock" if self.show.beat_locked else "free"
            parts.append(f"{self.show.bpm:5.1f} bpm {self.show.beat_source} {lock}")

        parts += [
            f"master {self._master:.2f}",
            source,
        ]

        # Where the colour is going, said permanently. UDP gives back no
        # evidence that anything is receiving it, so the most this can honestly
        # claim is the address it is aimed at - which is still the thing you
        # want on screen when the visuals are not moving.
        if self._osc is not None:
            parts.append(f"osc -> {self._osc.host}:{self._osc.port}")

        if self._blackout:
            parts.append("BLACKOUT")
        line = "   ·   ".join(parts)

        # A device with no wire, said permanently rather than once at startup.
        # It outlives _status on purpose: _status is cleared by the next control
        # that works, and "the sculpture is not plugged in" stays true through
        # every button press after it. Nothing else on screen distinguishes a
        # rig that is dark from a rig that is not there - the panel paints the
        # same colours either way, because the look really is being rendered
        # for it - so if this line does not say so, nothing does.
        offline = self.show.offline_devices
        if offline:
            line += "      " + ", ".join(f"{name} OFFLINE: {why}" for name, why in offline)

        # What the show warned about as it came up - a stale binary at the far
        # end of an ssh link, a channel claimed twice - said permanently for
        # the same reason: a cue that answers "unknown pattern" because the
        # executable predates it is not something the next click explains.
        warnings = self.show.warnings
        if warnings:
            line += "      " + " | ".join(warnings)

        if self._status:
            line += f"      {self._status}"

        self.header.configure(text=line, fg=TEXT_WARN if (self._status or offline or warnings) else TEXT)

    # -- controls ----------------------------------------------------------

    def _map_osc(self):
        """The mapping actions' link to the visualiser.

        --osc already made one, use it - one socket, one endpoint, and the
        colour tap and the scene bindings arrive as one peer. Otherwise make
        one at the defaults on first use, so a viewer opened with no OSC
        flags still fires Synesthesia bindings at the app next door.
        """
        if self._osc is not None:
            return self._osc
        if self._map_link is None:
            from .osc import SynesthesiaLink
            self._map_link = SynesthesiaLink(on_send=self.osc_traffic.saw_out)
        return self._map_link

    def _guard(self, action, describe: str) -> None:
        """Runs a control action, surfacing a refusal instead of dying on it."""
        try:
            action()
            self._status = ""
        except ShowError as error:
            self._status = f"{describe}: {error}"
        self._refresh_header()

    def _toggle_blackout(self) -> None:
        target = not self._blackout

        def apply() -> None:
            self.show.blackout(target)
            self._blackout = target

        self._guard(apply, "blackout")

    def _step_pattern(self, direction: int) -> None:
        if not self.pattern_names:
            self._status = "no pattern list; could not ask the executable"
            self._refresh_header()
            return

        if self.current_pattern in self.pattern_names:
            index = self.pattern_names.index(self.current_pattern)
        else:
            index = 0
        target = self.pattern_names[(index + direction) % len(self.pattern_names)]

        def apply() -> None:
            self.show.set_pattern(target)
            self.current_pattern = target

        self._guard(apply, "pattern")
        self._refresh_buttons()

    def _nudge_master(self, delta: float) -> None:
        self._set_master(max(0.0, min(1.0, self._master + delta)))

    def _set_master(self, target: float) -> None:
        def apply() -> None:
            self.show.set_master(target)
            self._master = target

        self._guard(apply, "master")

        # Keep the slider and the arrow keys showing the same number. Setting
        # the variable re-enters _on_master_slider, which is why that guards on
        # the value having actually moved.
        if hasattr(self, "_master_var"):
            self._master_var.set(round(self._master * 100.0))

        self._refresh_header()

    def _on_master_slider(self, value: str) -> None:
        try:
            target = float(value) / 100.0
        except ValueError:
            return

        # tk fires this on every pixel of the drag, and _set_master writes it
        # back into the variable, so without this the two bounce off each other.
        if abs(target - self._master) < 0.005:
            return

        self._set_master(target)

    def _nudge_speed(self, factor: float) -> None:
        target = max(0.001, self.config.pattern.speed * factor)

        def apply() -> None:
            self.show.set_speed(target)
            self.config.pattern.speed = target

        self._guard(apply, "speed")

    # -- lifecycle ---------------------------------------------------------

    def _quit(self) -> None:
        if self._closing:
            return
        self._closing = True

        # Same reason as the pump below: a playback tick queued on a window
        # being torn down would fire into a dead interpreter.
        try:
            self.curve_editor.stop_playback()
        except Exception:
            pass

        # An evening's bindings are worth more than a question on the way out.
        try:
            self.midi_panel.save_if_dirty()
        except Exception:
            pass

        # Before the show goes down: the lamps, the far end's wires and the
        # MIDI ports all talk through it.
        self._release_devices()

        # Cancel the pending redraw first. destroy() does not drop queued
        # `after` callbacks, so one would fire into a dead interpreter and
        # print a Tcl error over the top of a clean exit.
        if self._pump_id is not None:
            try:
                self.root.after_cancel(self._pump_id)
            except Exception:
                pass
            self._pump_id = None

        # Stop the show before tearing down the window, so the rig gets its
        # dark frame even if tk is mid-teardown.
        try:
            self.show.stop()
        except Exception:
            pass
        self.root.destroy()

    # -- client mode ---------------------------------------------------------

    def _open_sink(self, host: str) -> None:
        """Opens the far end's wires and starts feeding them this show.

        Never fatal, on the same reasoning as the lamps: a rig that cannot be
        reached is a desk that still runs, draws and speaks OSC. The reason is
        said once and the frames simply stop being forwarded.
        """
        try:
            sink = ShowController(
                self.config_path,
                dry_run=not self.live,
                remote=host,
                sink=True,
                emit_frames=False,
                autostart=False,
            )
            sink.start()
        except Exception as error:
            self._sink_failed = True
            self._say(f"client: {host}: {error}")
            return

        self._sink = sink
        self._say(f"client: driving {host}'s wires from here")

    def _send_to_sink(self, frame: Frame) -> None:
        """The newest frame, on to the far end's wires.

        Called from the reader thread with every frame the local show emits,
        so `--emit-rate` is what the rig is driven at - which has to be at
        least the fastest `device.fps` over there or the wires are being fed
        slower than they refresh.
        """
        sink = self._sink
        if sink is None or self._sink_failed:
            return
        try:
            sink.send_frame(frame)
        except Exception as error:
            # One failure stops the stream rather than throwing thirty times a
            # second into a pipe that is gone. The desk keeps running.
            self._sink_failed = True
            self._say(f"client: {error}; frames stopped")

    def _release_devices(self) -> None:
        """Give back every port this desk took, on any way out.

        Called from `_quit` and again from `run`'s finally, because they are
        not the same exit: closing the window runs the first, and Ctrl-C - the
        way the launcher tells you to end a set - unwinds straight past it out
        of `mainloop`. Leaving by that door used to leave the Launchpad in
        Programmer mode with the whole map still lit, and its own Setup button
        disabled, which is a controller you have to unplug to get back.

        Everything here is idempotent and guarded: it runs while the show may
        already be going down, and the second call must be a no-op rather than
        a traceback over the top of a clean exit.
        """
        self._close_lamps()
        self._close_sink()

        # And the input, explicitly. The port would go when the process does,
        # but "when the process does" is a race with whatever wants the port
        # next - and on the ALSA sequencer `eclipse-dmx IN` lingering is a
        # port Mixxx can still see and connect to after the desk has gone.
        if not self._midi_released:
            self._midi_released = True
            self._guard_quiet(lambda: self.show.command("midi close"))

    def _close_sink(self) -> None:
        if self._sink is None:
            return
        sink, self._sink = self._sink, None
        # `quit` first, so the far end sends its dark frame and gives the
        # relic its pixels back rather than being cut off mid-show.
        self._guard_quiet(sink.stop)

    # -- lamps -------------------------------------------------------------

    def _open_lamps(self, spec: str) -> None:
        """Opens the controller's output and takes the surface.

        Never fatal. A controller that is not there, a build with no libasound,
        a port another program already holds - each of those is a set that runs
        without lamps, which is a set that runs. The reason is said once and
        then dropped.
        """
        try:
            port = self.show.midi_out_open(spec)
        except ShowError as error:
            self._say(f"lamps: {error}")
            return

        try:
            # Live mode does not accept host LED messages - see launchpad.py.
            # Everything after this would half-work without it, which is worse
            # than not working: it looks like a failing cable.
            self.show.midi_send(programmer_mode(True))
            for message in lamp_clear():
                self.show.midi_send(message)
        except ShowError as error:
            self._say(f"lamps: {error}")
            self._guard_quiet(self.show.midi_out_close)
            return

        self._lamps_open = True
        self._lamps.forget()
        self._say(f"lamps: {port or spec}")
        self._paint_lamps()

    def _paint_lamps(self) -> None:
        """The surface, brought up to date. Cheap when nothing changed: the
        painter diffs and returns no messages, so this is safe to call from
        the pump."""
        if not self._lamps_open:
            return
        try:
            for message in self._lamps.frame(self._dispatcher.context):
                self.show.midi_send(message)
        except ShowError as error:
            # One failed repaint closes the lamps rather than retrying thirty
            # times a second down the wire the beat arrives on. The map still
            # fires; only the picture of it is gone.
            self._lamps_open = False
            self._say(f"lamps: {error}; lamps off")

    def _close_lamps(self) -> None:
        """Dark, and back to Live mode.

        The mode matters more than the darkness: Programmer mode disables the
        controller's own Setup button, so a desk that exits without putting it
        back leaves the box in a state its front panel cannot leave.
        """
        if not self._lamps_open:
            return
        self._lamps_open = False
        for send in (
            lambda: [self.show.midi_send(m) for m in lamp_clear()],
            lambda: self.show.midi_send(programmer_mode(False)),
            self.show.midi_out_close,
        ):
            self._guard_quiet(send)

    @staticmethod
    def _guard_quiet(action) -> None:
        """Run it, and let it fail. For the way out, where the show is already
        going down and there is nobody left to tell."""
        try:
            action()
        except Exception:
            pass

    def _start_osc_input(self) -> None:
        """Opens the OSC input port and binds it to this show.

        Never fatal, on any of its three failures - no map file, a map that
        will not parse, a port already held. Each is said in the status line
        and the window carries on: the rig is the show and this drives knobs
        on top of it.
        """
        from .osc_input import BindingSet, Dispatcher as OscDispatcher, OscListener

        bindings = BindingSet()
        if self._oscmap_path.exists():
            try:
                bindings = BindingSet.load(self._oscmap_path)
            except (OSError, ValueError) as error:
                self._say(f"osc map: {error}")
        else:
            self._say(f"osc map: no {self._oscmap_path.name}; listening with no bindings")

        self._osc_dispatcher = OscDispatcher(
            bindings, ActionContext(show=self.show, osc_factory=self._map_osc,
                                    say=self._say, syn=self._syn_state))

        port = self._osc_in_port or DEFAULT_INPUT_PORT
        try:
            self._osc_listener = OscListener(port, on_message=self._on_osc_in)
        except OSError as error:
            self._say(f"osc in: cannot listen on {port}: {error}")
            return

        live = sum(1 for one in bindings.bindings if one.enabled)
        self._say(f"osc in: port {port}, {live} of {len(bindings.bindings)} bindings live")

        # What to type at the far end, and whether this machine will let it in -
        # both said before anything has gone wrong rather than after. See
        # osc_input_hints.
        from .cli import osc_input_hints
        for note in osc_input_hints(port):
            self._say(note)

    def _on_osc_in(self, address: str, arguments) -> None:
        """Called on the listener's thread. Touches no widget.

        `_say` is the one thing it does reach for, and that is already what
        the reader thread uses for the same reason - see _send_osc.
        """
        # Recorded before it is dispatched, and whether or not any binding
        # wants it: an address nothing takes is precisely what the panel is
        # for, and one filtered out here would be invisible there.
        self.osc_traffic.saw_in(address, arguments)

        if self._osc_dispatcher is None:
            return
        for line in self._osc_dispatcher.handle(address, arguments):
            self._say(line)

    def _install_signal_handlers(self) -> None:
        """Ctrl-C and a plain `kill` end the set the same way the window does.

        The launcher says "Ctrl-C ends the set; the rig is put dark on the way
        out", and that has to include the controller: without this, SIGINT
        unwinds out of mainloop with the Launchpad still in Programmer mode
        and the map still lit.

        Queued with `after_idle` rather than run in the handler, because a
        signal lands between two bytecodes wherever the interpreter happens to
        be - possibly inside tk - and tearing the window down from there is
        how a clean exit becomes a crash. Falls back to running it directly if
        tk will not take the callback, which is the case when the signal
        arrives before mainloop.

        Only on the main thread, and only where the signals exist: a viewer
        constructed inside a worker (a test) must not touch process-wide
        handlers.
        """
        import signal
        import threading

        if threading.current_thread() is not threading.main_thread():
            return

        def bail(_signum, _frame):
            # Only a flag. A signal lands between two bytecodes wherever the
            # interpreter happens to be - very often inside tk, since that is
            # where this process spends its life - and both tearing the window
            # down from there and scheduling onto tk's own queue from there
            # are re-entrant into Tcl. The pump is already running every 16ms
            # and is a safe place to be told.
            self._bail_requested = True

        for name in ("SIGINT", "SIGTERM", "SIGHUP"):
            handler = getattr(signal, name, None)
            if handler is None:
                continue
            try:
                # Only where nothing has claimed it. A host that installed
                # its own handler - a supervisor, a test runner catching
                # Ctrl-C - meant it, and quietly replacing it would break the
                # thing it was for.
                #
                # `default_int_handler` counts as unclaimed: it is what python
                # itself puts on SIGINT at startup, so treating it as somebody
                # else's would skip the one signal this is most for.
                unclaimed = (signal.SIG_DFL, None,
                             getattr(signal, "default_int_handler", None))
                if signal.getsignal(handler) not in unclaimed:
                    continue
                signal.signal(handler, bail)
            except (ValueError, OSError):
                # Not the main thread of the process, or a platform without
                # it. Closing the window still works; this was the extra door.
                pass

    def run(self) -> int:
        try:
            self.root.mainloop()
        finally:
            # First, and before the show is stopped: these speak through it.
            # `_quit` has usually done this already and it is a no-op, but the
            # door out of mainloop is not always `_quit` - a Ctrl-C or an
            # exception in a callback comes through here and nowhere else.
            self._guard_quiet(self._release_devices)
            try:
                self.show.stop()
            except Exception:
                pass
            if self._osc is not None:
                try:
                    self._osc.close()
                except Exception:
                    pass
            if self._map_link is not None:
                try:
                    self._map_link.close()
                except Exception:
                    pass
            if self._osc_listener is not None:
                try:
                    self._osc_listener.close()
                except Exception:
                    pass
        return 0


def view(
    config_path: Union[str, Path],
    executable: Optional[Union[str, Path]] = None,
    pattern: Optional[str] = None,
    state: Optional[str] = None,
    live: bool = False,
    emit_rate: float = 30.0,
    midi: Optional[str] = None,
    bpm: Optional[float] = None,
    osc=None,
    osc_device: Optional[str] = None,
    osc_fixture: int = 0,
    midimap: Optional[Union[str, Path]] = None,
    midi_out: Optional[str] = None,
    client: Optional[str] = None,
    oscmap: Optional[Union[str, Path]] = None,
    osc_in: Optional[int] = None,
    host: Optional[str] = None,
    remote_command: Optional[str] = None,
) -> int:
    """Opens the viewer on a config and blocks until the window closes."""
    app = ViewerApp(
        config_path,
        executable=executable,
        pattern=pattern,
        state=state,
        live=live,
        emit_rate=emit_rate,
        midi=midi,
        bpm=bpm,
        osc=osc,
        osc_device=osc_device,
        osc_fixture=osc_fixture,
        midimap=midimap,
        midi_out=midi_out,
        client=client,
        oscmap=oscmap,
        osc_in=osc_in,
        host=host,
        remote_command=remote_command,
    )
    return app.run()
