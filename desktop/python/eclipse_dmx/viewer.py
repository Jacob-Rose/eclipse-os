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
import threading
import time
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple, Union

from .config import Config
from .controller import Frame, ShowController, ShowError
from .patterns import list_patterns

RGB = Tuple[int, int, int]

# ===========================================================================
# The buttons. This is the bit to edit.
# ===========================================================================
#
# Hardcoded on purpose rather than generated from whatever the executable
# reports: a generated row gives every look equal weight and alphabetical
# order, and what you actually want at a desk is the four you are using
# tonight, first, with the names you call them.
#
# Each entry is (label, command). A command is one of:
#
#     ("state",   "campfire")     switch a state machine to that state
#     ("pattern", "obelisk_seasons")   switch the whole pattern
#     ("input",   "a")            hold one of the two momentary inputs while
#                                 the button is held down
#     ("beat",    "")             a downbeat, now - tap it in
#     ("bpm",     "128")          set the tempo outright
#
# Rows are drawn in order, one strip of buttons per row. Add, remove and
# reorder freely; nothing else needs to change. A state the running pattern
# does not offer is dimmed rather than hidden, so a row can hold looks from
# more than one machine.

STATE_BUTTONS: List[List[Tuple[str, Tuple[str, str]]]] = [
    # --- mythos26 -----------------------------------------------------
    # slot_5 onwards are still placeholders. Rename them here when they are
    # renamed in makeMythos26StateMachine().
    [
        ("pulse", ("state", "beat_pulse")),
        ("vu pulse", ("state", "vu_pulse")),
        ("static b/w", ("state", "tv_static_mono")),
        ("static rgb", ("state", "tv_static")),
        ("slot 5", ("state", "slot_5")),
        ("slot 6", ("state", "slot_6")),
        ("slot 7", ("state", "slot_7")),
    ],
    # --- the jacket's looks -------------------------------------------
    [
        ("void", ("state", "digital_void")),
        ("forest", ("state", "enchanted_forest")),
        ("turbines", ("state", "warp_turbines")),
        ("rainbow", ("state", "rainbow_road")),
        ("breathe", ("state", "breathe_with_me")),
        ("parrot", ("state", "parrot")),
    ],
    [
        ("overload", ("state", "system_overload")),
        ("toxin", ("state", "cyber_toxin")),
        ("datamine", ("state", "datamine")),
        ("bluemagic", ("state", "blue_magic")),
        ("campfire", ("state", "campfire")),
        ("hitstop", ("state", "hitstop")),
    ],
]

#: Held down, not toggled - these are momentary, like the remote buttons the
#: jacket's looks were written around.
INPUT_BUTTONS: List[Tuple[str, str]] = [
    ("input A", "a"),
    ("input B", "b"),
]

#: Offered when the running pattern is not a state machine, so the window is
#: still useful for the plain looks.
PATTERN_BUTTONS: List[Tuple[str, Tuple[str, str]]] = [
    ("mythos26", ("pattern", "mythos26")),
    ("jacket", ("pattern", "jacket")),
    ("seasons", ("pattern", "obelisk_seasons")),
    ("theater", ("pattern", "obelisk_theater")),
    ("rainbow", ("pattern", "rainbow")),
    ("chase", ("pattern", "chase")),
    ("identify", ("pattern", "identify")),
]

#: Tempo, for when there is no MIDI or the phase has drifted. "tap" is a
#: downbeat now: hit it on the one and the rig lines up.
TEMPO_BUTTONS: List[Tuple[str, Tuple[str, str]]] = [
    ("tap", ("beat", "")),
    ("100", ("bpm", "100")),
    ("120", ("bpm", "120")),
    ("128", ("bpm", "128")),
    ("140", ("bpm", "140")),
]

#: How often the beat-driven looks fire. "auto" hands each look back its own
#: default, which for beat_pulse is every beat and for vu_pulse is every other.
DIVISION_BUTTONS: List[Tuple[str, Tuple[str, str]]] = [
    ("auto", ("div", "0")),
    ("on 1", ("div", "1")),
    ("on 2", ("div", "2")),
    ("on 4", ("div", "4")),
]

# ===========================================================================

# A dark room, so the lights are the brightest thing on screen.
BACKGROUND = (11, 13, 16)
PANEL = "#14181d"
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
    ys = normalise([point[1] for point in raw])

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
        width: int = 1000,
        # Room for four rows of cue buttons under the canvas. The knobs sit
        # beside them rather than below, so this does not grow with them.
        height: int = 600,
    ) -> None:
        self.config_path = Path(config_path)
        self.config = Config.load(self.config_path)
        self.config.validate(strict_overlap=False)

        self.placements = plan_layout(self.config)
        self.live = live

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

        #: Selected beat division, as the button value. "0" is each look's own
        #: default, which is what the executable starts on.
        self._division = "0"

        self.show = ShowController(
            self.config_path,
            executable=executable,
            dry_run=not live,
            on_frame=self._on_frame,
            emit_rate=emit_rate,
            midi=midi,
            bpm=bpm,
            autostart=False,
        )

        self._build_window(width, height)

        self.show.start()
        if pattern:
            self.show.set_pattern(pattern)
            self.current_pattern = pattern
        else:
            self.current_pattern = self.config.pattern.name

        if state:
            # Not fatal if it does not take: a bad name should leave a working
            # window with a message, not refuse to open.
            self._guard(lambda: self.show.set_state(state), "state")

        self._refresh_header()
        self._pump_id = self.root.after(16, self._pump)

    # -- window ------------------------------------------------------------

    def _build_window(self, width: int, height: int) -> None:
        self.root = tk.Tk()
        self.root.title(f"eclipse-dmx  -  {self.config_path.name}")
        self.root.configure(bg=PANEL)
        self.root.geometry(f"{width}x{height}")
        self.root.minsize(480, 280)

        self.header = tk.Label(
            self.root, anchor="w", bg=PANEL, fg=TEXT, font=("Consolas", 11), padx=12, pady=8
        )
        self.header.pack(fill="x")

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
            "[←]/[→] speed   [t] tap the beat   [q] quit",
        )
        self.footer.pack(side="bottom", fill="x")

        self._build_buttons()

        self.canvas = tk.Canvas(
            self.root, bg="#%02x%02x%02x" % BACKGROUND, highlightthickness=0
        )
        self.canvas.pack(fill="both", expand=True)

        self.canvas.bind("<Configure>", lambda event: self._rebuild_items())

        self.root.bind("<space>", lambda event: self._toggle_blackout())
        self.root.bind("<Key-n>", lambda event: self._step_pattern(1))
        self.root.bind("<Key-p>", lambda event: self._step_pattern(-1))
        self.root.bind("<Up>", lambda event: self._nudge_master(0.05))
        self.root.bind("<Down>", lambda event: self._nudge_master(-0.05))
        self.root.bind("<Right>", lambda event: self._nudge_speed(1.25))
        self.root.bind("<Left>", lambda event: self._nudge_speed(0.8))
        self.root.bind("<Key-t>", lambda event: self._run_button(("beat", "")))
        self.root.bind("<Key-q>", lambda event: self._quit())
        self.root.bind("<Escape>", lambda event: self._quit())
        self.root.protocol("WM_DELETE_WINDOW", self._quit)

        self._items: List[dict] = []
        self._rebuild_items()

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

        self._state_rows: List[tk.Frame] = []
        self._state_buttons: Dict[str, tk.Button] = {}

        for row in STATE_BUTTONS:
            frame = tk.Frame(self.button_panel, bg=PANEL)
            for label, command in row:
                button = tk.Button(
                    frame, text=label, font=("Consolas", 9),
                    bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                    activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
                    highlightthickness=0, borderwidth=0,
                    command=lambda c=command: self._run_button(c),
                )
                button.pack(side="left", padx=3, pady=3)
                if command[0] == "state":
                    self._state_buttons[command[1]] = button
            self._state_rows.append(frame)

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

        self.extra_row.pack(fill="x")

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

        tk.Label(self.tempo_row, text="   fire ", bg=PANEL, fg=TEXT_DIM,
                 font=("Consolas", 9)).pack(side="left")

        self._division_buttons: Dict[str, tk.Button] = {}
        for label, command in DIVISION_BUTTONS:
            button = tk.Button(
                self.tempo_row, text=label, font=("Consolas", 9),
                bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                activeforeground=BUTTON_FG, relief="flat", padx=8, pady=3,
                highlightthickness=0, borderwidth=0,
                command=lambda c=command: self._run_button(c),
            )
            button.pack(side="left", padx=3, pady=3)
            self._division_buttons[command[1]] = button

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

        # -- the running look's own knobs, in the right pane ----------------
        # Everything on the left is fixed furniture. This panel is not: what it
        # holds comes from whatever is running, over the protocol.
        tk.Label(self.param_pane, text="look", bg=PANEL, fg=TEXT_DIM,
                 font=("Consolas", 9), anchor="w", padx=8).pack(fill="x", pady=(4, 0))

        self.param_panel = tk.Frame(self.param_pane, bg=PANEL)
        self.param_panel.pack(fill="both", expand=True, padx=4, pady=2)
        self._param_widgets: Dict[str, Tuple[tk.Variable, Optional[tk.Entry]]] = {}
        self._param_sent: Dict[str, float] = {}
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

            # Enough for the widest cue row on the left and one column of knobs
            # on the right. Both halves have a natural width and they add up to
            # about the window, so this is close to what a drag would land on
            # anyway - it just saves doing it on every launch.
            self.split.sash_place(0, int(self.split.winfo_width() * 0.68), 0)

    # -- the running look's knobs -----------------------------------------

    def _build_params(self) -> None:
        """Builds a control per property the running look offers.

        Rebuilt from scratch whenever the set changes, unlike the state buttons
        which are packed and unpacked: *which* knobs exist is a property of the
        look, so there is no stable set of widgets to keep around. A cue change
        replaces the panel.

        A float gets a slider and a box. The slider is for finding a value and
        the box is for saying one - a 0..3 slider 110 pixels wide cannot express
        0.15, and an envelope tuned to the nearest pixel is not tuned.
        """
        for child in self.param_panel.winfo_children():
            child.destroy()
        self._param_widgets = {}
        self._param_sent = {}

        params = list(self.show.params)
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

    def _apply_param(self, name: str, value: float) -> None:
        self._param_sent[name] = value
        self._guard(lambda: self.show.set_param(name, value), f"param {name}")

        # The executable clamps to the range and echoes what it landed on, so
        # the widgets follow the look rather than the other way round.
        self._show_param(name)

    def _show_param(self, name: str) -> None:
        param = self.show.get_param(name)
        widgets = self._param_widgets.get(name)
        if param is None or widgets is None:
            return

        variable, entry = widgets
        self._param_writing = True
        try:
            variable.set(1 if (param.is_bool and param.value) else (0 if param.is_bool else param.value))
            if entry is not None:
                entry.delete(0, "end")
                entry.insert(0, f"{param.value:g}")
        finally:
            self._param_writing = False
        self._param_sent[name] = param.value

    def _refresh_buttons(self) -> None:
        """Shows the state rows only when the pattern actually has states."""
        has_states = bool(self.show.state_names)

        for frame in self._state_rows:
            if has_states and not frame.winfo_ismapped():
                frame.pack(fill="x", before=self.extra_row)
            elif not has_states and frame.winfo_ismapped():
                frame.pack_forget()

        # a state the running machine does not offer is dimmed, not hidden:
        # the table is yours, and silently dropping an entry would read as a bug
        for name, button in self._state_buttons.items():
            known = name in self.show.state_names
            active = known and name == self.show.current_state
            button.configure(
                bg=BUTTON_BG_ACTIVE if active else BUTTON_BG,
                fg=BUTTON_FG if known else TEXT_DIM,
            )

        self._refresh_division_buttons()
        self._resize_split()

    def _run_button(self, command: Tuple[str, str]) -> None:
        kind, value = command

        def apply() -> None:
            if kind == "state":
                self.show.set_state(value)
            elif kind == "pattern":
                self.show.set_pattern(value)
                self.current_pattern = value
            elif kind == "beat":
                self.show.tap_beat()
            elif kind == "bpm":
                self.show.set_bpm(float(value))
            elif kind == "div":
                self.show.set_beat_division(int(value))
                self._division = value

        self._guard(apply, kind)
        self._refresh_buttons()
        self._refresh_division_buttons()

    def _refresh_division_buttons(self) -> None:
        """Lights the selected division, and dims them all when nothing running
        pulses on the beat."""
        available = bool(self.show.state_names)
        for value, button in self._division_buttons.items():
            active = available and value == self._division
            button.configure(
                bg=BUTTON_BG_ACTIVE if active else BUTTON_BG,
                fg=BUTTON_FG if available else TEXT_DIM,
            )

    def _set_input(self, channel: str, down: bool) -> None:
        if not self.show.state_names:
            return
        self._guard(lambda: self.show.set_input(channel, down), "input")
        button = self._input_buttons.get(channel)
        if button is not None:
            button.configure(bg=BUTTON_BG_ACTIVE if down else BUTTON_BG)

    def _rebuild_items(self) -> None:
        """Lays the fixtures out for the current window size.

        Canvas items are created once and recoloured per frame. Deleting and
        recreating 90 items thirty times a second is visible as flicker.
        """
        self.canvas.delete("all")
        self._items = []

        width = max(self.canvas.winfo_width(), 1)
        height = max(self.canvas.winfo_height(), 1)
        if width <= 1 or height <= 1:
            return

        label_room = 30
        margin_x = width * 0.09
        margin_y = height * 0.13

        span_x = max(width - 2 * margin_x, 1.0)
        span_y = max(height - 2 * margin_y - label_room, 1.0)

        points = [
            (margin_x + place.x * span_x, margin_y + place.y * span_y)
            for place in self.placements
        ]

        # Checked before the closest-pair scan below, which is quadratic and
        # would be measuring a gap the dense path does not use.
        if len(self.placements) > DENSE_ABOVE:
            self._build_dense_items(points)
            self._paint(self._current_frame(), force=True)
            return

        # Size the fixtures to the gap between the closest pair, so a dense rig
        # does not draw as one smear and a sparse one does not draw as
        # pinpricks. The budget is the *glow*, not the disc: neighbouring beams
        # should meet, not overlap, or the rig reads as a single wash and you
        # lose the per-fixture reading that is the whole point of looking.
        closest = min(span_x, span_y)
        for i, (ax, ay) in enumerate(points):
            for bx, by in points[i + 1:]:
                closest = min(closest, ((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5)

        radius = closest * 0.5 / GLOW_EXTENT

        # Keep the outermost ring inside the canvas: an edge fixture clipped by
        # the frame looks like a dark fixture.
        radius = min(radius, margin_x / GLOW_EXTENT, margin_y / GLOW_EXTENT)
        radius = max(min(radius, 44.0), 3.0)

        for place, (cx, cy) in zip(self.placements, points):
            rings = []
            for index in range(GLOW_RINGS):
                # index 0 is the outermost, faintest ring
                t = index / max(GLOW_RINGS - 1, 1)
                ring_radius = radius * (GLOW_EXTENT - (GLOW_EXTENT - 1.0) * t)
                rings.append(
                    (
                        self.canvas.create_oval(
                            cx - ring_radius,
                            cy - ring_radius,
                            cx + ring_radius,
                            cy + ring_radius,
                            outline="",
                            fill="#%02x%02x%02x" % BACKGROUND,
                        ),
                        0.05 + 0.95 * (t ** 2.4),
                    )
                )

            core = self.canvas.create_oval(
                cx - radius,
                cy - radius,
                cx + radius,
                cy + radius,
                outline="",
                fill="#%02x%02x%02x" % BACKGROUND,
            )

            self.canvas.create_text(
                cx,
                cy + radius * GLOW_EXTENT + 12,
                text=place.name,
                fill=TEXT,
                font=("Consolas", 8),
            )
            self.canvas.create_text(
                cx,
                cy + radius * GLOW_EXTENT + 24,
                text=f"@{place.address}",
                fill=TEXT_DIM,
                font=("Consolas", 8),
            )

            self._items.append({"rings": rings, "core": core})

        # Repaint immediately so a resize does not blank the rig until the next
        # frame arrives - which, if the show has ended, is never.
        self._paint(self._current_frame(), force=True)

    def _build_dense_items(self, points: List[Tuple[float, float]]) -> None:
        """One dot per node, for a rig too dense to draw as beams.

        Labels go on the *run* rather than the node: 344 names is not a legend,
        it is a smear. A run is a group of consecutive nodes sharing a name
        before the index the patcher appended - which on the obelisk is exactly
        one strip up one side, the unit you actually want to pick out.
        """
        # Dots want to nearly touch, so the strip reads as a strip. The vertical
        # gap is the tight one: eight columns of 43 in a landscape canvas.
        gaps = [
            ((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5
            for (ax, ay), (bx, by) in zip(points, points[1:])
            if (ax - bx) ** 2 + (ay - by) ** 2 > 0.0
        ]
        radius = max(min((min(gaps) if gaps else 6.0) * 0.55, 9.0), 1.5)

        for (cx, cy) in points:
            core = self.canvas.create_oval(
                cx - radius,
                cy - radius,
                cx + radius,
                cy + radius,
                outline="",
                fill="#%02x%02x%02x" % BACKGROUND,
            )
            self._items.append({"rings": (), "core": core})

        runs: Dict[str, List[float]] = {}
        for place, (cx, _) in zip(self.placements, points):
            runs.setdefault(_run_name(place.name), []).append(cx)

        label_y = max(y for _, y in points) + radius + 14
        for name, xs in runs.items():
            self.canvas.create_text(
                sum(xs) / len(xs),
                label_y,
                text=name,
                fill=TEXT_DIM,
                font=("Consolas", 8),
            )

    # -- the show ----------------------------------------------------------

    def _on_frame(self, frame: Frame) -> None:
        """Called on the reader thread. Does not touch tk."""
        with self._lock:
            self._latest = frame
            self._frames_seen += 1

    def _current_frame(self) -> Optional[Frame]:
        with self._lock:
            return self._latest

    def _pump(self) -> None:
        """The tk-side loop: drain the newest frame, redraw, measure."""
        self._pump_id = None

        if self._closing:
            return

        if not self.show.is_running:
            self._status = "the show has ended"
            self._refresh_header()
            self._paint(None)
            return

        with self._lock:
            frame = self._latest
            seen = self._frames_seen

        self._paint(frame)

        # STATES and STATE arrive on the reader thread, whenever the executable
        # gets round to them, so the buttons follow from here rather than from
        # the click that caused it.
        signature = (tuple(self.show.state_names), self.show.current_state)
        if signature != self._button_signature:
            self._button_signature = signature
            self._refresh_buttons()
            self._refresh_header()

        # Same reason, for the knobs: the executable announces a new set on
        # every pattern and state change, and the revision is what says so.
        if self.show.params_revision != self._param_revision:
            self._param_revision = self.show.params_revision
            self._build_params()

        now = time.monotonic()
        elapsed = now - self._fps_marker
        if elapsed >= 0.5:
            self._fps = (seen - self._fps_counted) / elapsed
            self._fps_counted = seen
            self._fps_marker = now
            self._refresh_header()

        self._pump_id = self.root.after(16, self._pump)

    def _paint(self, frame: Optional[Frame], force: bool = False) -> None:
        # Repainting the frame already on screen costs one itemconfig per item
        # and buys nothing. It is free to skip on a ten-par rig and it is the
        # difference between smooth and not on a 344-pixel one, where the pump
        # runs at 60Hz over a 30fps frame stream and half the passes are
        # redundant by construction. `force` is for a resize, where the items
        # are new and the frame is not.
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

    def _refresh_header(self) -> None:
        source = "LIVE" if self.live else "dry-run"
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

        if self._division != "0":
            parts.append(f"on {self._division}")
        if self._blackout:
            parts.append("BLACKOUT")
        line = "   ·   ".join(parts)
        if self._status:
            line += f"      {self._status}"

        self.header.configure(text=line, fg=TEXT_WARN if self._status else TEXT)

    # -- controls ----------------------------------------------------------

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

    def run(self) -> int:
        try:
            self.root.mainloop()
        finally:
            try:
                self.show.stop()
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
    )
    return app.run()
