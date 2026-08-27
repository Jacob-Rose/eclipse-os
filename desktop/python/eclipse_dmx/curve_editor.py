"""The curve editor: a timeline for eanim's automation curves, on the desk.

A secondary view under the device panels - toggled from the button band - for
drawing the AutomationCurve shapes the relics play from an impulse. The value
axis is a 0..1 weight; aim it at any of the running look's float knobs (or the
master) and press play, and the curve drives that knob live, on the same rig
picture the cue buttons drive. That loop is the point: shape a hit against the
actual look instead of imagining it, then copy the addKey calls out and paste
them into a state.

The maths is eclipse_dmx.curves, which mirrors the C++ exactly - including the
eight-key cap, so nothing drawable here fails to fit on the sculpture.

Tooling, all on the canvas:

    double-click       add a key (up to the sculpture's eight)
    drag a key         move it; the list re-sorts around it
    right-click a key  delete it
    drag the ruler     scrub - the value goes out while you drag
    easing menu        the shape of the segment leaving the selected key
"""

from __future__ import annotations

import time
import tkinter as tk
from typing import Callable, List, Optional, Tuple

from .curves import LINEAR, SEGMENT_SHAPES, Curve, CurveKey, example_hit

# the viewer's palette - keep in step with viewer.py (importing it from there
# would be circular: the viewer imports this)
BACKGROUND = (11, 13, 16)
PANEL = "#14181d"
PANEL_EDGE = "#2a323b"
TEXT = "#c8d0d8"
TEXT_DIM = "#6b7783"
BUTTON_BG = "#232a32"
BUTTON_BG_ACTIVE = "#3d6ea5"
BUTTON_FG = "#c8d0d8"

#: the curve itself, and its keys
CURVE = "#5fa8e8"
KEY_FILL = "#c8d0d8"
KEY_SELECTED = "#e8a33d"
PLAYHEAD = "#e8a33d"
GRID = "#1b2127"

#: A target the editor can aim a curve at: (name, minimum, maximum). The
#: curve's 0..1 is mapped onto the range when a value is sent.
Target = Tuple[str, float, float]

#: The master fader, always offered: every look has one even when it has no
#: knobs of its own, so the editor is demonstrable on anything.
MASTER: Target = ("master", 0.0, 1.0)

#: How a live-shape target is spelled in the aim menu. A knob target streams
#: evaluated values at the look; a shape target *is* one of the look's own
#: AutomationCurves - drawing here rewrites the curve the look plays.
SHAPE_PREFIX = "~ "


class CurveEditor:
    """The timeline band. Owns a Curve, draws it, plays it at a knob."""

    CANVAS_HEIGHT = 170
    #: the strip along the top that scrubs instead of editing
    RULER = 16
    #: room on the left for the value labels
    GUTTER = 34
    #: how close a click must land to a key to pick it up, in pixels
    GRAB = 7

    #: the playback clock, roughly the emit rate; tk's after is not a
    #: metronome and the evaluate is against wall time, so jitter here moves
    #: sample points, not the shape
    TICK_MS = 33

    def __init__(self, parent: tk.Widget,
                 on_send: Callable[[str, float], None],
                 on_status: Callable[[str], None],
                 on_send_curve=None,
                 on_load_curve=None) -> None:
        #: called with (target name, mapped value) for every sample sent
        self._on_send = on_send
        #: one line to the viewer's header, for refusals and confirmations
        self._on_status = on_status
        #: called with (curve name, key tuples) when a live shape is edited
        self._on_send_curve = on_send_curve
        #: called with a curve name; returns the look's current key tuples
        self._on_load_curve = on_load_curve

        self.curve: Curve = example_hit()
        self.selected: Optional[CurveKey] = None

        self._targets: List[Target] = [MASTER]
        #: live-shape targets the running look offers, by bare name
        self._curve_names: List[str] = []
        #: the live shape being edited, or None when aimed at a knob
        self._aimed_curve: Optional[str] = None

        self._dragging: Optional[CurveKey] = None
        self._scrubbing = False

        #: seconds across the visible timeline; follows the curve, below
        self._span = 2.0

        self._play_started: Optional[float] = None
        self._play_after: Optional[str] = None

        self.frame = tk.Frame(parent, bg=PANEL, highlightthickness=1,
                              highlightbackground=PANEL_EDGE)

        self._build_toolbar()

        self.canvas = tk.Canvas(self.frame, bg="#%02x%02x%02x" % BACKGROUND,
                                height=self.CANVAS_HEIGHT, highlightthickness=0)
        self.canvas.pack(fill="x", padx=4, pady=(0, 4))

        self.canvas.bind("<Configure>", lambda event: self.redraw())
        self.canvas.bind("<ButtonPress-1>", self._on_press)
        self.canvas.bind("<B1-Motion>", self._on_motion)
        self.canvas.bind("<ButtonRelease-1>", self._on_release)
        self.canvas.bind("<Double-Button-1>", self._on_double)
        self.canvas.bind("<Button-3>", self._on_right)

    # -- toolbar -----------------------------------------------------------

    def _build_toolbar(self) -> None:
        bar = tk.Frame(self.frame, bg=PANEL)
        bar.pack(fill="x", padx=4, pady=2)

        tk.Label(bar, text="curve ", bg=PANEL, fg=TEXT_DIM,
                 font=("Consolas", 9)).pack(side="left")

        # -- which knob the curve drives -----------------------------------
        self._target_var = tk.StringVar(value=MASTER[0])
        self._target_menu = tk.OptionMenu(bar, self._target_var, MASTER[0])
        self._style_menu(self._target_menu, width=14)
        self._target_menu.pack(side="left", padx=(0, 2))

        #: the mapped range, so "0..1 onto this knob" is visible not implied
        self._range_label = tk.Label(bar, text="", bg=PANEL, fg=TEXT_DIM,
                                     font=("Consolas", 9))
        self._range_label.pack(side="left", padx=(0, 8))

        # -- transport ------------------------------------------------------
        self._play_button = tk.Button(
            bar, text="play", font=("Consolas", 9), width=5,
            bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
            activeforeground=BUTTON_FG, relief="flat", padx=8, pady=2,
            highlightthickness=0, borderwidth=0, command=self.toggle_playback,
        )
        self._play_button.pack(side="left", padx=2)

        self._loop_var = tk.IntVar(value=1)
        tk.Checkbutton(
            bar, text="loop", variable=self._loop_var,
            bg=PANEL, fg=TEXT, selectcolor=BUTTON_BG,
            activebackground=PANEL, activeforeground=TEXT,
            font=("Consolas", 9), highlightthickness=0, borderwidth=0,
        ).pack(side="left", padx=2)

        # -- the selected key's outgoing segment ---------------------------
        tk.Label(bar, text="   ease ", bg=PANEL, fg=TEXT_DIM,
                 font=("Consolas", 9)).pack(side="left")

        self._easing_var = tk.StringVar(value=LINEAR)
        self._easing_writing = False
        self._easing_menu = tk.OptionMenu(bar, self._easing_var, *SEGMENT_SHAPES,
                                          command=lambda _value: self._on_easing_chosen())
        self._style_menu(self._easing_menu, width=16)
        self._easing_menu.pack(side="left", padx=(0, 8))

        # -- the way out ----------------------------------------------------
        for label, command in (("copy c++", self._copy_cpp),
                               ("example", self._load_example),
                               ("clear", self._clear)):
            tk.Button(
                bar, text=label, font=("Consolas", 9),
                bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                activeforeground=BUTTON_FG, relief="flat", padx=8, pady=2,
                highlightthickness=0, borderwidth=0, command=command,
            ).pack(side="left", padx=2)

        tk.Label(
            bar, bg=PANEL, fg=TEXT_DIM, font=("Consolas", 8),
            text="dbl-click add · drag move · right-click delete · drag ruler to scrub",
        ).pack(side="right", padx=4)

    def _style_menu(self, menu: tk.OptionMenu, width: int) -> None:
        menu.configure(bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                       activeforeground=BUTTON_FG, relief="flat", width=width,
                       font=("Consolas", 9), highlightthickness=0, borderwidth=0,
                       indicatoron=False, padx=8, pady=3)
        menu["menu"].configure(bg=BUTTON_BG, fg=BUTTON_FG,
                               activebackground=BUTTON_BG_ACTIVE,
                               activeforeground=BUTTON_FG, font=("Consolas", 9))

    # -- targets -----------------------------------------------------------

    def set_targets(self, targets: List[Target], curve_names=()) -> None:
        """What the running look offers: float knobs, and its live shapes.

        Called whenever the look changes. The current aim survives the change
        when the new look has a target of the same name; otherwise it falls
        back to the master rather than silently driving something that no
        longer exists. An aim that survives on a *shape* reloads it, because
        the same name on a new look is a different curve.
        """
        self._targets = [MASTER] + [t for t in targets if t[0] != MASTER[0]]
        self._curve_names = list(curve_names)

        menu = self._target_menu["menu"]
        menu.delete(0, "end")
        for name, _low, _high in self._targets:
            menu.add_command(label=name,
                             command=lambda n=name: self._aim_at(n))
        for name in self._curve_names:
            label = SHAPE_PREFIX + name
            menu.add_command(label=label,
                             command=lambda n=label: self._aim_at(n))

        valid = {name for name, _l, _h in self._targets}
        valid.update(SHAPE_PREFIX + name for name in self._curve_names)
        if self._target_var.get() not in valid:
            self._target_var.set(MASTER[0])
            self._aimed_curve = None
        elif self._aimed_curve is not None:
            self._load_live(self._aimed_curve)
        self._refresh_range_label()

    def _aim_at(self, name: str) -> None:
        self._target_var.set(name)
        if name.startswith(SHAPE_PREFIX):
            self._aimed_curve = name[len(SHAPE_PREFIX):]
            self.stop_playback()
            self._load_live(self._aimed_curve)
        else:
            self._aimed_curve = None
        self._refresh_range_label()

    def refresh_live(self) -> None:
        """Reloads the aimed shape from the look.

        For after something else rewrote it - the attack and decay knobs
        rebuild the envelope over a drawn shape - so the picture follows the
        look rather than the other way round. A drag in progress is left
        alone; the hand wins over the refresh.
        """
        if self._aimed_curve is not None and self._dragging is None:
            self._load_live(self._aimed_curve)

    def _load_live(self, name: str) -> None:
        """The look's current shape, into the editor - so an edit starts from
        what is actually playing rather than from a blank."""
        keys = self._on_load_curve(name) if self._on_load_curve else None
        if not keys:
            return

        loaded = Curve()
        for key_time, key_value, easing in keys:
            loaded.add_key(key_time, key_value, easing or LINEAR)
        self.curve = loaded
        self._select(None)
        self._fit_span()
        self.redraw()

    def _target(self) -> Target:
        wanted = self._target_var.get()
        return next((t for t in self._targets if t[0] == wanted), MASTER)

    def _refresh_range_label(self) -> None:
        if self._aimed_curve is not None:
            # not a mapped range: edits rewrite the look's own curve
            self._range_label.configure(text="live shape")
            return
        _name, low, high = self._target()
        self._range_label.configure(text=f"{low:g}..{high:g}")

    # -- sending -----------------------------------------------------------

    def _send_at(self, seconds: float) -> None:
        """The curve's value at `seconds`, mapped onto the target and sent.

        Clamped to 0..1 first: an overshooting easing (Back, Elastic) is drawn
        overshooting, because that is its shape, but a knob has a range and
        the executable would clamp anyway - doing it here keeps the number on
        the wire the number the rig uses.

        Aimed at a live shape, this sends nothing: the look plays its own
        curve on its own triggers, and the transport here is just a preview.
        """
        if self._aimed_curve is not None:
            return
        weight = max(0.0, min(1.0, self.curve.evaluate(seconds)))
        name, low, high = self._target()
        self._on_send(name, low + weight * (high - low))

    def _push_shape(self) -> None:
        """The drawn shape, into the look it belongs to.

        Called after each completed edit - a drag released, a key added or
        deleted, an easing chosen - never per motion event: a shape is one
        edit, and the look should see finished shapes, not the mouse.
        """
        if self._aimed_curve is None or self._on_send_curve is None:
            return
        if len(self.curve.keys) < 2:
            # an empty or one-key curve is a flat line the look cannot play;
            # keep drawing, send when it is a shape again
            return

        self._on_send_curve(self._aimed_curve, [
            (key.time, key.value, None if key.easing == LINEAR else key.easing)
            for key in self.curve.keys
        ])

    # -- playback ----------------------------------------------------------

    def toggle_playback(self) -> None:
        if self._play_started is None:
            self.start_playback()
        else:
            self.stop_playback()

    def start_playback(self) -> None:
        if len(self.curve.keys) < 2 or self.curve.duration <= 0.0:
            self._on_status("curve: nothing to play - it needs two keys apart in time")
            return
        self._play_started = time.monotonic()
        self._play_button.configure(text="stop", bg=BUTTON_BG_ACTIVE)
        self._play_tick()

    def stop_playback(self) -> None:
        if self._play_after is not None:
            try:
                self.frame.after_cancel(self._play_after)
            except Exception:
                pass
            self._play_after = None
        self._play_started = None
        self._play_button.configure(text="play", bg=BUTTON_BG)
        self.redraw()

    def _play_tick(self) -> None:
        self._play_after = None
        if self._play_started is None:
            return

        elapsed = time.monotonic() - self._play_started
        duration = self.curve.duration
        if duration <= 0.0:
            # the keys were edited away mid-play
            self.stop_playback()
            return

        if elapsed >= duration:
            if self._loop_var.get():
                # slide the clock rather than resetting it, so a pass's
                # overshoot carries into the next instead of quantising to now
                laps = int(elapsed / duration)
                self._play_started += laps * duration
                elapsed -= laps * duration
            else:
                # land exactly on the end, so the knob finishes where the
                # curve says rather than wherever the last tick sampled
                self._send_at(duration)
                self.stop_playback()
                return

        self._send_at(elapsed)
        self.redraw(playhead=elapsed)
        self._play_after = self.frame.after(self.TICK_MS, self._play_tick)

    # -- geometry ----------------------------------------------------------

    def _fit_span(self) -> None:
        """The timeline shows the curve plus room to grow.

        The slack past the last key is what makes the tail draggable further
        right: dragging against the edge grows the span on the next redraw,
        so the timeline follows the drag instead of walling it.
        """
        duration = self.curve.duration
        self._span = max(2.0, duration * 1.25)

    def _plot_rect(self) -> Tuple[float, float, float, float]:
        """left, top, width, height of the plotted 0..1 x 0..span area."""
        width = max(self.canvas.winfo_width(), 1)
        height = max(self.canvas.winfo_height(), 1)
        left = self.GUTTER
        top = self.RULER + 4
        return (left, top,
                max(width - left - 12, 1),
                max(height - top - 18, 1))

    def _to_px(self, seconds: float, value: float) -> Tuple[float, float]:
        left, top, width, height = self._plot_rect()
        return (left + (seconds / self._span) * width,
                top + (1.0 - value) * height)

    def _from_px(self, x: float, y: float) -> Tuple[float, float]:
        left, top, width, height = self._plot_rect()
        return (max(0.0, (x - left) / width * self._span),
                max(0.0, min(1.0, 1.0 - (y - top) / height)))

    def _key_at(self, x: float, y: float) -> Optional[CurveKey]:
        for key in self.curve.keys:
            px, py = self._to_px(key.time, key.value)
            if abs(px - x) <= self.GRAB and abs(py - y) <= self.GRAB:
                return key
        return None

    # -- interaction -------------------------------------------------------

    def _on_press(self, event: "tk.Event") -> None:
        if event.y <= self.RULER:
            self._scrubbing = True
            self._scrub_to(event.x)
            return

        key = self._key_at(event.x, event.y)
        if key is not None:
            self._select(key)
            self._dragging = key
        self.redraw()

    def _on_motion(self, event: "tk.Event") -> None:
        if self._scrubbing:
            self._scrub_to(event.x)
            return
        if self._dragging is None:
            return

        seconds, value = self._from_px(event.x, event.y)
        self._dragging.time = seconds
        self._dragging.value = value
        # keep time order live during the drag, so the drawn curve is always
        # the curve the keys describe - selection follows the object, not an
        # index, so it survives the resort
        self.curve.resort()
        self._fit_span()
        self.redraw()

    def _on_release(self, event: "tk.Event") -> None:
        finished_drag = self._dragging is not None
        self._dragging = None
        self._scrubbing = False
        self.redraw()
        if finished_drag:
            self._push_shape()

    def _on_double(self, event: "tk.Event") -> None:
        if event.y <= self.RULER or self._key_at(event.x, event.y) is not None:
            return

        seconds, value = self._from_px(event.x, event.y)
        if not self.curve.add_key(seconds, value):
            # the sculpture's cap, not the editor's - see Curve.MAX_KEYS
            self._on_status(f"curve: full - a relic curve holds {Curve.MAX_KEYS} keys")
            return

        added = next(key for key in self.curve.keys
                     if key.time == seconds and key.value == value)
        self._select(added)
        self._fit_span()
        self.redraw()
        self._push_shape()

    def _on_right(self, event: "tk.Event") -> None:
        key = self._key_at(event.x, event.y)
        if key is None:
            return
        self.curve.remove_key(key)
        if self.selected is key:
            self._select(None)
        self._fit_span()
        self.redraw()
        self._push_shape()

    def _scrub_to(self, x: float) -> None:
        seconds, _value = self._from_px(x, 0.0)
        seconds = min(seconds, self._span)
        self._send_at(seconds)
        self.redraw(playhead=seconds)

    def _select(self, key: Optional[CurveKey]) -> None:
        self.selected = key
        self._easing_writing = True
        try:
            self._easing_var.set(key.easing if key is not None else LINEAR)
        finally:
            self._easing_writing = False

    def _on_easing_chosen(self) -> None:
        if self._easing_writing or self.selected is None:
            return
        self.selected.easing = self._easing_var.get()
        self.redraw()
        self._push_shape()

    # -- tooling -----------------------------------------------------------

    def _copy_cpp(self) -> None:
        if not self.curve.keys:
            self._on_status("curve: nothing to copy")
            return
        text = self.curve.to_cpp()
        self.frame.clipboard_clear()
        self.frame.clipboard_append(text)
        self._on_status(
            f"curve: {len(self.curve.keys)} addKey call(s) on the clipboard")

    def _load_example(self) -> None:
        self.curve = example_hit()
        self._select(None)
        self._fit_span()
        self.redraw()
        self._push_shape()

    def _clear(self) -> None:
        self.curve.clear()
        self._select(None)
        self._fit_span()
        self.redraw()

    # -- drawing -----------------------------------------------------------

    def _time_step(self) -> float:
        """A ruler tick spacing that keeps labels apart and numbers round."""
        _left, _top, width, _height = self._plot_rect()
        for step in (0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0):
            if (step / self._span) * width >= 44:
                return step
        return 10.0

    def redraw(self, playhead: Optional[float] = None) -> None:
        """The whole band, from scratch.

        A full delete-and-redraw, unlike the device panels' recolouring: a
        curve is a couple of hundred canvas items at most and only redraws on
        interaction or a 30Hz playhead, so itemconfig bookkeeping would buy
        nothing here.
        """
        canvas = self.canvas
        canvas.delete("all")

        left, top, width, height = self._plot_rect()

        # value grid: quarters, labelled at 0, 1/2 and 1
        for quarter in range(5):
            value = quarter / 4.0
            _x, y = self._to_px(0.0, value)
            canvas.create_line(left, y, left + width, y, fill=GRID)
            if quarter % 2 == 0:
                canvas.create_text(left - 6, y, text=f"{value:g}", anchor="e",
                                   fill=TEXT_DIM, font=("Consolas", 7))

        # time grid and the ruler labels
        step = self._time_step()
        ticks = int(self._span / step) + 1
        for tick in range(ticks + 1):
            seconds = tick * step
            if seconds > self._span:
                break
            x, _y = self._to_px(seconds, 0.0)
            canvas.create_line(x, top, x, top + height, fill=GRID)
            canvas.create_text(x + 2, self.RULER - 6, text=f"{seconds:g}s",
                               anchor="w", fill=TEXT_DIM, font=("Consolas", 7))

        # the ruler strip's floor, so the scrub area reads as a thing
        canvas.create_line(0, self.RULER, left + width, self.RULER, fill=PANEL_EDGE)

        if self.curve.keys:
            # the curve, sampled every couple of pixels - cheap, and exact
            # enough that a key sits visibly *on* the line it shapes
            points: List[float] = []
            samples = max(int(width / 2), 2)
            for sample in range(samples + 1):
                seconds = (sample / samples) * self._span
                x, y = self._to_px(seconds, self.curve.evaluate(seconds))
                points += [x, y]
            canvas.create_line(*points, fill=CURVE, width=2)

            for key in self.curve.keys:
                x, y = self._to_px(key.time, key.value)
                size = 4
                fill = KEY_SELECTED if key is self.selected else KEY_FILL
                canvas.create_rectangle(x - size, y - size, x + size, y + size,
                                        fill=fill, outline="")
        else:
            canvas.create_text(left + width / 2, top + height / 2,
                               text="double-click to add a key",
                               fill=TEXT_DIM, font=("Consolas", 9))

        # duration, under the plot: what the copy button will hand to a relic
        canvas.create_text(left, top + height + 9, anchor="w",
                           text=f"{len(self.curve.keys)}/{Curve.MAX_KEYS} keys · "
                                f"{self.curve.duration:.2f}s",
                           fill=TEXT_DIM, font=("Consolas", 8))

        if playhead is not None:
            x, _y = self._to_px(playhead, 0.0)
            canvas.create_line(x, self.RULER, x, top + height, fill=PLAYHEAD)
