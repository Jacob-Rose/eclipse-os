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

import threading
import time
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence, Tuple, Union

from .config import Config
from .controller import Frame, ShowController, ShowError
from .patterns import list_patterns

RGB = Tuple[int, int, int]

# A dark room, so the lights are the brightest thing on screen.
BACKGROUND = (11, 13, 16)
PANEL = "#14181d"
TEXT = "#c8d0d8"
TEXT_DIM = "#6b7783"
TEXT_WARN = "#e8a33d"

#: Rings drawn around each fixture to fake a beam. tkinter has no alpha, so the
#: falloff is opaque circles blended toward the background by hand.
GLOW_RINGS = 8
GLOW_EXTENT = 2.6  # outermost ring, in multiples of the fixture radius


def _blend(color: RGB, background: RGB, alpha: float) -> str:
    """`color` over `background` at `alpha`, as a tk hex string."""
    return "#%02x%02x%02x" % tuple(
        max(0, min(255, int(round(c * alpha + b * (1.0 - alpha)))))
        for c, b in zip(color, background)
    )


@dataclass
class Placement:
    """Where one fixture sits on screen, and what to call it."""

    name: str
    address: int
    x: float  # 0..1 across the canvas
    y: float  # 0..1 down the canvas


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
        live: bool = False,
        emit_rate: float = 30.0,
        width: int = 1000,
        height: int = 420,
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

        self.show = ShowController(
            self.config_path,
            executable=executable,
            dry_run=not live,
            on_frame=self._on_frame,
            emit_rate=emit_rate,
            autostart=False,
        )

        self._build_window(width, height)

        self.show.start()
        if pattern:
            self.show.set_pattern(pattern)
            self.current_pattern = pattern
        else:
            self.current_pattern = self.config.pattern.name

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

        self.canvas = tk.Canvas(
            self.root, bg="#%02x%02x%02x" % BACKGROUND, highlightthickness=0
        )
        self.canvas.pack(fill="both", expand=True)

        self.footer = tk.Label(
            self.root,
            anchor="w",
            bg=PANEL,
            fg=TEXT_DIM,
            font=("Consolas", 9),
            padx=12,
            pady=6,
            text="[space] blackout   [n]/[p] pattern   [↑]/[↓] master   "
            "[←]/[→] speed   [q] quit",
        )
        self.footer.pack(fill="x")

        self.canvas.bind("<Configure>", lambda event: self._rebuild_items())

        self.root.bind("<space>", lambda event: self._toggle_blackout())
        self.root.bind("<Key-n>", lambda event: self._step_pattern(1))
        self.root.bind("<Key-p>", lambda event: self._step_pattern(-1))
        self.root.bind("<Up>", lambda event: self._nudge_master(0.05))
        self.root.bind("<Down>", lambda event: self._nudge_master(-0.05))
        self.root.bind("<Right>", lambda event: self._nudge_speed(1.25))
        self.root.bind("<Left>", lambda event: self._nudge_speed(0.8))
        self.root.bind("<Key-q>", lambda event: self._quit())
        self.root.bind("<Escape>", lambda event: self._quit())
        self.root.protocol("WM_DELETE_WINDOW", self._quit)

        self._items: List[dict] = []
        self._rebuild_items()

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
        self._paint(self._current_frame())

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

        now = time.monotonic()
        elapsed = now - self._fps_marker
        if elapsed >= 0.5:
            self._fps = (seen - self._fps_counted) / elapsed
            self._fps_counted = seen
            self._fps_marker = now
            self._refresh_header()

        self._pump_id = self.root.after(16, self._pump)

    def _paint(self, frame: Optional[Frame]) -> None:
        for index, item in enumerate(self._items):
            color: RGB = (0, 0, 0)
            if frame is not None and index < len(frame):
                color = frame[index]

            for oval, alpha in item["rings"]:
                self.canvas.itemconfig(oval, fill=_blend(color, BACKGROUND, alpha))
            self.canvas.itemconfig(item["core"], fill="#%02x%02x%02x" % color)

    def _refresh_header(self) -> None:
        source = "LIVE" if self.live else "dry-run"
        parts = [
            f"{self.current_pattern}",
            f"{len(self.placements)} fixtures",
            f"{self._fps:4.1f} fps",
            f"master {self._master:.2f}",
            source,
        ]
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

    def _nudge_master(self, delta: float) -> None:
        target = max(0.0, min(1.0, self._master + delta))

        def apply() -> None:
            self.show.set_master(target)
            self._master = target

        self._guard(apply, "master")

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
    live: bool = False,
    emit_rate: float = 30.0,
) -> int:
    """Opens the viewer on a config and blocks until the window closes."""
    app = ViewerApp(
        config_path,
        executable=executable,
        pattern=pattern,
        live=live,
        emit_rate=emit_rate,
    )
    return app.run()
