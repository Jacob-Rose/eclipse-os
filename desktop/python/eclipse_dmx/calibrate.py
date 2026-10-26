"""The beep test: how late is the rig, in milliseconds, and bake it in.

A beat reaches a lamp late. It is rendered on the next frame, crosses a
network in client mode, sits in a DMX frame that takes 23ms to leave the
widget, and then the fixture answers at its own pace. None of that is
visible from the desk except as the lights being *just* behind the music,
every beat, all night - which is what `midi.latency_ms` pays back: the clock
is read that far ahead, and the flash lands on the kick.

This is the screen that finds the number, and it is Rock Band's screen. A
click on the desk's own speakers, the rig flashing white on the same beat,
and two ways to line them up - both live, on the real wires, network and all:

  by feel   the arrow keys move the latency a millisecond at a time until
            the flash and the click land together. Ten at a time with shift.

  by tap    tap along to the click for a few bars, then to the flash. Where
            the taps land differs by exactly how late the flash is: the
            reaction time is in both and cancels. `a` applies the answer.

`s` writes it to the config, and every show from that file runs with it.

What the click cannot do is leave the desk's own audio path: it goes out
through the same sound stack the music does, at a small fixed latency of
its own, so the number found is "the rig, relative to a sound played here".
That is the right number for a set played from here, and it can be trimmed
by ear with the same arrow keys - `latency nudge` on the protocol - while
the set is running.
"""

from __future__ import annotations

import array
import math
import os
import shutil
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Callable, List, Optional, Sequence, Tuple, Union

from .config import ConfigError, LATENCY_LIMIT_MS, format_latency_ms, write_midi_latency
from .controller import FrameForwarder, ShowController, ShowError

#: The tempo the test runs at. Anything would do; 120 makes a beat a round
#: half second, which makes the numbers on screen easy to sanity-check.
DEFAULT_BPM = 120.0

#: What one arrow press moves the latency by, and one with shift held.
STEP_MS = 1.0
BIG_STEP_MS = 10.0

#: Taps in a round, and how many at the front are the person finding the
#: groove rather than measuring it.
TAPS_PER_ROUND = 12
SETTLING_TAPS = 2

# -- the click's sound ------------------------------------------------------
RATE = 48000
CHUNK = 480             #: samples per write: 10ms
AHEAD = 0.03            #: seconds the stream is written ahead of real time
CLICK_SECONDS = 0.03
CLICK_HZ = 1000.0
ACCENT_HZ = 1500.0      #: the one of the bar, so the ear can count
BEATS_PER_BAR = 4

#: WM class the window announces itself under; see viewer.WM_CLASS.
WM_CLASS = "eclipse-dmx"


# ===========================================================================
# The grid: the beat, on this machine's clock
# ===========================================================================

class Grid:
    """The beat as a line on `time.monotonic()`: an anchor and a period.

    Owned here, not read back from the executable. The show's clock is put
    on this line once, with a single `beat` at the anchor - stamped by its
    reader thread as the line lands, so the two agree to well under a
    millisecond - and both then free-run on the same monotonic clock, so
    they stay agreed. Everything measured is measured against this.
    """

    def __init__(self, bpm: float, anchor: Optional[float] = None) -> None:
        self.period = 60.0 / float(bpm)
        self.anchor = time.monotonic() if anchor is None else anchor

    @property
    def bpm(self) -> float:
        return 60.0 / self.period

    def beat_at(self, index: int) -> float:
        return self.anchor + index * self.period

    def nearest(self, at: float) -> Tuple[int, float]:
        """The beat closest to `at`, and how far after it `at` is, in
        seconds: negative is early."""
        index = int(round((at - self.anchor) / self.period))
        return index, at - self.beat_at(index)

    def beats_between(self, start: float, end: float) -> List[Tuple[int, float]]:
        """Every (index, time) with start <= time < end."""
        first = math.ceil((start - self.anchor) / self.period)
        out = []
        index = first
        while True:
            at = self.beat_at(index)
            if at >= end:
                return out
            out.append((index, at))
            index += 1


# ===========================================================================
# The click
# ===========================================================================

def click_waveform(hertz: float, seconds: float = CLICK_SECONDS,
                   rate: int = RATE) -> "array.array[int]":
    """A short sine with a fast decay: a click with a pitch, not a thud.

    The decay is what makes the onset the only thing the ear can latch on
    to. A tone that held its level would be tapped to somewhere in the
    middle of it.
    """
    count = int(rate * seconds)
    out = array.array("h", [0] * count)
    for i in range(count):
        envelope = math.exp(-6.0 * i / count)
        out[i] = int(0.6 * 32767 * envelope * math.sin(2.0 * math.pi * hertz * i / rate))
    return out


def render_chunk(grid: Grid, start: float, count: int, click: Sequence[int],
                 accent: Sequence[int], rate: int = RATE,
                 muted: bool = False) -> bytes:
    """`count` samples of the click track, starting at monotonic time `start`.

    A pure function of the grid, which is what keeps the clicks sample-
    accurate against each other however unevenly the writes land: a chunk
    asks where the beats are and draws them, it does not count.
    """
    out = array.array("h", bytes(count * 2))
    if muted:
        return out.tobytes()

    longest = max(len(click), len(accent))
    window_start = start - longest / rate
    window_end = start + count / rate
    for index, at in grid.beats_between(window_start, window_end):
        shape = accent if index % BEATS_PER_BAR == 0 else click
        offset = int(round((at - start) * rate))
        lo = max(0, offset)
        hi = min(count, offset + len(shape))
        for i in range(lo, hi):
            out[i] = shape[i - offset]
    return out.tobytes()


class Click:
    """A click on every beat of a grid, out of the desk's speakers.

    Streamed, not fired: raw PCM is written to a player a few milliseconds
    ahead of real time, and the clicks are drawn into that stream where the
    grid says they go. Firing a sound per beat would put a process start or
    a playback call's own scheduling on every one of them; a stream costs
    one fixed latency at the start, and the same one every beat.

    That latency is the player's buffer plus `AHEAD`, and it is deliberately
    small - tens of milliseconds, about what the music itself has going
    through the same stack - because it is the one part of this the test
    cannot see past. `muted` keeps the stream going and the clicks out of it.
    """

    #: Players tried in order, each reading raw s16 mono at RATE on stdin.
    #: pipewire's first, then pulse's (which pipewire also answers), then
    #: bare ALSA for a machine with neither.
    PLAYERS: Sequence[Tuple[str, Sequence[str]]] = (
        ("pw-cat", ("pw-cat", "-p", "--raw", "--format", "s16", "--rate", str(RATE),
                    "--channels", "1", "--latency", "20ms", "-")),
        ("pacat", ("pacat", "--playback", "--raw", "--format=s16le", f"--rate={RATE}",
                   "--channels=1", "--latency-msec=20")),
        ("aplay", ("aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", str(RATE),
                   "-c", "1", "--buffer-time=20000", "-")),
    )

    def __init__(self, grid: Grid) -> None:
        self.grid = grid
        self.muted = False
        self.player: Optional[str] = None
        self.why = "not started"
        self._process: Optional[subprocess.Popen] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._click = click_waveform(CLICK_HZ)
        self._accent = click_waveform(ACCENT_HZ)

    @property
    def available(self) -> bool:
        return self._process is not None and self._process.poll() is None

    def describe(self) -> str:
        return f"click: {self.player}" if self.available else f"no click: {self.why}"

    def start(self) -> bool:
        """Opens the first player that stays open. False, and `why`, if none."""
        if sys.platform == "win32":
            self.why = "no raw PCM player on windows; run the test on the linux desk"
            return False

        tried = []
        for name, command in self.PLAYERS:
            if shutil.which(command[0]) is None:
                continue
            tried.append(name)
            try:
                process = subprocess.Popen(
                    list(command), stdin=subprocess.PIPE,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, bufsize=0)
            except OSError:
                continue
            # Prime it with silence and see whether it is still there: a
            # player with no server to talk to exits at once.
            try:
                process.stdin.write(bytes(CHUNK * 2 * 3))
                time.sleep(0.15)
            except (BrokenPipeError, OSError):
                pass
            if process.poll() is not None:
                continue
            self._process = process
            self.player = name
            break

        if self._process is None:
            names = ", ".join(name for name, _ in self.PLAYERS)
            self.why = (f"none of {names} stayed open (tried {', '.join(tried) or 'nothing found'}); "
                        f"is an audio server running?")
            return False

        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="click", daemon=True)
        self._thread.start()
        return True

    def stop(self) -> None:
        self._stop.set()
        thread = self._thread
        if thread is not None:
            thread.join(timeout=1.0)
        process = self._process
        self._process = None
        if process is not None:
            try:
                process.stdin.close()
            except OSError:
                pass
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                process.kill()

    def _run(self) -> None:
        # Sample 0 goes into the pipe now and is heard one buffer later. The
        # writes are paced so the pipe never holds more than AHEAD of audio:
        # that, not the pipe's 64K, is what bounds the latency.
        origin = time.monotonic()
        written = 0
        process = self._process
        while not self._stop.is_set() and process is not None:
            start = origin + written / RATE
            wait = (start - AHEAD) - time.monotonic()
            if wait > 0:
                time.sleep(wait)
            data = render_chunk(self.grid, start, CHUNK, self._click, self._accent,
                                muted=self.muted)
            try:
                process.stdin.write(data)
            except (BrokenPipeError, OSError):
                self.why = f"{self.player} closed the stream"
                return
            written += CHUNK


# ===========================================================================
# Tapping
# ===========================================================================

class TapRound:
    """One run of taps against one cue - the click, or the flash.

    Each tap is where it landed relative to the nearest beat of the grid, in
    milliseconds, positive being late. The first few are not counted: they
    are the person finding the groove, and they land anywhere.
    """

    def __init__(self, cue: str) -> None:
        self.cue = cue
        self.offsets_ms: List[float] = []

    def add(self, offset_ms: float) -> None:
        self.offsets_ms.append(offset_ms)

    @property
    def counted(self) -> List[float]:
        return self.offsets_ms[SETTLING_TAPS:]

    @property
    def done(self) -> bool:
        return len(self.offsets_ms) >= TAPS_PER_ROUND

    @property
    def mean_ms(self) -> Optional[float]:
        counted = self.counted
        return statistics.fmean(counted) if counted else None

    @property
    def spread_ms(self) -> Optional[float]:
        counted = self.counted
        return statistics.pstdev(counted) if len(counted) >= 2 else None

    def describe(self) -> str:
        mean = self.mean_ms
        if mean is None:
            return f"{self.cue}: not measured"
        spread = self.spread_ms
        wobble = f" ±{spread:.0f}" if spread is not None else ""
        return f"{self.cue}: {len(self.counted)} taps, {mean:+.0f}ms{wobble}"


def suggested_latency_ms(current_ms: float, beep_mean_ms: float,
                         flash_mean_ms: float) -> float:
    """The latency that puts the flash where the click is.

    Taps to the click land at (click latency + reaction); taps to the flash
    at (rig latency - current lead + reaction). The reaction is in both and
    goes; what is left is how far the flash trails the click, which is
    exactly how much further ahead the clock should be read.
    """
    return max(-LATENCY_LIMIT_MS,
               min(LATENCY_LIMIT_MS, current_ms + (flash_mean_ms - beep_mean_ms)))


# ===========================================================================
# The screen
# ===========================================================================

class CalibrationApp:
    """The beep test, as a window on a running show.

    The show is started the way `run` starts one - the same executable, the
    same `--client` - so the frames take the same road to the lamps that a
    set's do, and the number measured is the number a set needs.
    """

    def __init__(
        self,
        config_path: Union[str, Path],
        executable: Optional[Union[str, Path]] = None,
        dry_run: bool = False,
        client: Optional[str] = None,
        bpm: float = DEFAULT_BPM,
        emit_rate: float = 30.0,
        click: bool = True,
        on_log: Optional[Callable[[str], None]] = None,
    ) -> None:
        import tkinter as tk

        self.config_path = Path(config_path)
        self.client_host = (client or "").strip()
        self.bpm = float(bpm)
        self._closing = False
        self._pump_id = None
        self._messages: List[str] = []

        # -- the show ---------------------------------------------------------
        self._sink: Optional[FrameForwarder] = None
        if self.client_host:
            self._sink = FrameForwarder(self.config_path, self.client_host,
                                        live=not dry_run, say=self._say)

        self.show = ShowController(
            self.config_path,
            executable=executable,
            dry_run=dry_run,
            midi="",                 # nothing else may move the clock
            bpm=self.bpm,
            pattern="metronome",
            on_log=on_log,
            emit_frames=self._sink is not None,
            emit_rate=emit_rate,
            on_frame=self._sink.send if self._sink is not None else None,
        )
        self.warnings: List[str] = list(self.show.warnings)
        if self._sink is not None:
            self._sink.open()

        self.show.set_free_run(True)
        self.show.set_bpm(self.bpm)
        self.latency_ms = self.show.get_latency()
        self.saved_ms: Optional[float] = self.latency_ms

        # -- the grid, and the show put on it ---------------------------------
        # The anchor is read just before the line is written, and the reader
        # thread over there stamps it as it lands: a pipe's worth apart.
        anchor = time.monotonic()
        self.show.tap_beat()
        self.grid = Grid(self.bpm, anchor)

        # -- the rounds --------------------------------------------------------
        self.rounds = {"beep": TapRound("beep"), "flash": TapRound("flash")}
        self.active: Optional[TapRound] = None

        # -- the click ---------------------------------------------------------
        # Started after the window exists: Tcl's own start-up holds the
        # interpreter for a tenth of a second, and a stream already running
        # would have stalled that long on its first beat.
        self.click: Optional[Click] = Click(self.grid) if click else None
        self._build_window(tk)
        if self.click is not None and not self.click.start():
            self._say(self.click.describe())

        self._refresh()
        self._pump_id = self.root.after(16, self._pump)

    # -- window ------------------------------------------------------------

    def _build_window(self, tk) -> None:
        from .viewer import BUTTON_BG, PANEL, TEXT, TEXT_DIM, TEXT_WARN

        self.root = tk.Tk(className=os.environ.get("ECLIPSE_DMX_WM_CLASS", WM_CLASS))
        where = f"  ->  {self.client_host}" if self.client_host else ""
        self.root.title(f"eclipse-dmx  -  calibrate  -  {self.config_path.name}{where}")
        self.root.configure(bg=PANEL)
        self.root.geometry("720x420")
        self.root.minsize(560, 360)
        self._colors = {"panel": PANEL, "text": TEXT, "dim": TEXT_DIM,
                        "warn": TEXT_WARN, "button": BUTTON_BG}

        self.header = tk.Label(self.root, anchor="w", bg=PANEL, fg=TEXT,
                               font=("Consolas", 11), padx=12, pady=8)
        self.header.pack(fill="x")

        # The number. Big, because it is the whole screen.
        self.number = tk.Label(self.root, bg=PANEL, fg=TEXT, font=("Consolas", 44, "bold"))
        self.number.pack(pady=(18, 0))
        self.number_note = tk.Label(self.root, bg=PANEL, fg=TEXT_DIM, font=("Consolas", 9))
        self.number_note.pack()

        # A dot that pulses on the grid: proof the metronome is running, and
        # nothing more - it is drawn by tk whenever tk gets to it, and is not
        # what anyone should be tapping to. Hidden during a round for that
        # reason.
        self.dot = tk.Canvas(self.root, width=36, height=36, bg=PANEL,
                             highlightthickness=0)
        self._dot_item = self.dot.create_oval(4, 4, 32, 32, fill=BUTTON_BG, outline="")
        self.dot.pack(pady=10)

        self.round_line = tk.Label(self.root, bg=PANEL, fg=TEXT, font=("Consolas", 11),
                                   justify="center")
        self.round_line.pack(pady=(4, 0))
        self.result_line = tk.Label(self.root, bg=PANEL, fg=TEXT_DIM, font=("Consolas", 10),
                                    justify="center")
        self.result_line.pack(pady=(2, 0))

        self.footer = tk.Label(
            self.root, anchor="w", justify="left", bg=PANEL, fg=TEXT_DIM,
            font=("Consolas", 9), padx=12, pady=6,
            text=("[up]/[down] 1ms   [shift] 10ms   [space] tap\n"
                  "[b] tap to the beep   [f] tap to the flash   [a] apply what they say\n"
                  "[s] save to the config   [r] start the rounds over   [q] quit"))
        self.footer.pack(side="bottom", fill="x")
        self.message = tk.Label(self.root, anchor="w", bg=PANEL, fg=TEXT_WARN,
                                font=("Consolas", 9), padx=12)
        self.message.pack(side="bottom", fill="x")

        self.root.bind("<Up>", lambda e: self.nudge(BIG_STEP_MS if e.state & 0x1 else STEP_MS))
        self.root.bind("<Down>", lambda e: self.nudge(-(BIG_STEP_MS if e.state & 0x1 else STEP_MS)))
        self.root.bind("<space>", lambda e: self.tap())
        self.root.bind("b", lambda e: self.start_round("beep"))
        self.root.bind("f", lambda e: self.start_round("flash"))
        self.root.bind("a", lambda e: self.apply())
        self.root.bind("s", lambda e: self.save())
        self.root.bind("r", lambda e: self.reset_rounds())
        self.root.bind("q", lambda e: self._quit())
        self.root.bind("<Escape>", lambda e: self._quit())
        self.root.protocol("WM_DELETE_WINDOW", self._quit)

    def _say(self, line: str) -> None:
        self._messages.append(line)
        print(line, file=sys.stderr)
        label = getattr(self, "message", None)
        if label is not None:
            try:
                label.configure(text=line)
            except Exception:
                pass

    # -- the latency -------------------------------------------------------

    def set_latency(self, milliseconds: float) -> None:
        """Puts the clock this far ahead, live."""
        self.latency_ms = self.show.set_latency(milliseconds)
        self._refresh()

    def nudge(self, milliseconds: float) -> None:
        self.latency_ms = self.show.nudge_latency(milliseconds)
        self._refresh()

    # -- tapping -------------------------------------------------------------

    def start_round(self, cue: str) -> None:
        """Begins tapping to `cue`, with the other cue taken away.

        The rig goes dark while the ear is being measured, and the click is
        muted while the eye is: each cue on its own, or a person taps to
        whichever they noticed first and the two rounds measure the same
        thing.
        """
        if self.active is not None:
            self._end_round()
        self.rounds[cue] = TapRound(cue)
        self.active = self.rounds[cue]
        if cue == "beep":
            self.show.blackout(True)
        elif self.click is not None:
            self.click.muted = True
        self._refresh()

    def tap(self, at: Optional[float] = None) -> None:
        """A tap, at `at` on the monotonic clock - now, when not given."""
        at = time.monotonic() if at is None else at
        if self.active is None:
            self._say("no round running: [b] to tap to the beep, [f] to the flash")
            return
        _, offset = self.grid.nearest(at)
        self.active.add(offset * 1000.0)
        if self.active.done:
            self._end_round()
        self._refresh()

    def _end_round(self) -> None:
        if self.active is None:
            return
        if self.active.cue == "beep":
            try:
                self.show.blackout(False)
            except ShowError:
                pass
        if self.click is not None:
            self.click.muted = False
        self.active = None

    def reset_rounds(self) -> None:
        self._end_round()
        self.rounds = {"beep": TapRound("beep"), "flash": TapRound("flash")}
        self._refresh()

    @property
    def suggestion_ms(self) -> Optional[float]:
        """What the two rounds say the latency should be, once both are in."""
        beep = self.rounds["beep"].mean_ms
        flash = self.rounds["flash"].mean_ms
        if beep is None or flash is None or self.active is not None:
            return None
        return suggested_latency_ms(self.latency_ms, beep, flash)

    def apply(self) -> None:
        suggestion = self.suggestion_ms
        if suggestion is None:
            self._say("nothing to apply yet: tap a round to the beep and one to the flash")
            return
        self.set_latency(round(suggestion))
        self._say(f"latency {format_latency_ms(self.latency_ms)}ms - applied, not saved: [s] saves it")

    def save(self) -> None:
        try:
            written = write_midi_latency(self.config_path, self.latency_ms)
        except (OSError, ValueError, ConfigError) as error:
            self._say(f"could not write {self.config_path.name}: {error}")
            return
        self.saved_ms = self.latency_ms
        self._say(f"{self.config_path.name}: {written}")
        self._refresh()

    # -- drawing -------------------------------------------------------------

    def _refresh(self) -> None:
        colors = self._colors
        click = self.click.describe() if self.click is not None else "no click"
        where = f"  ->  {self.client_host}" if self.client_host else ""
        self.header.configure(
            text=f"calibrate  {self.config_path.name}{where}   {self.bpm:g} bpm   {click}")

        self.number.configure(text=f"latency  {format_latency_ms(self.latency_ms)} ms")
        if self.saved_ms is None or abs(self.saved_ms - self.latency_ms) > 0.05:
            self.number_note.configure(text="not saved  -  [s] writes midi.latency_ms",
                                       fg=colors["warn"])
        else:
            self.number_note.configure(text="saved in the config", fg=colors["dim"])

        active = self.active
        if active is not None:
            cue = "click" if active.cue == "beep" else "flash"
            self.round_line.configure(
                text=f"tap [space] on every {cue}   {len(active.offsets_ms)}/{TAPS_PER_ROUND}")
        else:
            self.round_line.configure(text="[b] tap to the beep    [f] tap to the flash")

        beep, flash = self.rounds["beep"], self.rounds["flash"]
        lines = [f"{beep.describe()}    {flash.describe()}"]
        suggestion = self.suggestion_ms
        if suggestion is not None:
            trail = flash.mean_ms - beep.mean_ms
            lines.append(f"the flash lands {trail:+.0f}ms from the click  ->  latency "
                         f"{format_latency_ms(suggestion)}ms   [a] applies it")
        self.result_line.configure(text="\n".join(lines))

    def _pump(self) -> None:
        if self._closing:
            return
        if not self.show.is_running:
            self._say("the show stopped")
            self._quit()
            return

        # The dot: lit for the first fifth of every beat, off during a round.
        _, since = self.grid.nearest(time.monotonic())
        lit = self.active is None and 0.0 <= since < self.grid.period * 0.2
        self.dot.itemconfigure(self._dot_item,
                               fill=self._colors["text"] if lit else self._colors["button"])
        self._pump_id = self.root.after(16, self._pump)

    # -- lifecycle -----------------------------------------------------------

    def _quit(self) -> None:
        if self._closing:
            return
        self._closing = True
        if self._pump_id is not None:
            try:
                self.root.after_cancel(self._pump_id)
            except Exception:
                pass
        self._release()
        try:
            self.root.destroy()
        except Exception:
            pass

    def _release(self) -> None:
        if self.click is not None:
            try:
                self.click.stop()
            except Exception:
                pass
        try:
            self.show.stop()
        except Exception:
            pass
        if self._sink is not None:
            try:
                self._sink.close()
            except Exception:
                pass

    def run(self) -> int:
        try:
            self.root.mainloop()
        finally:
            self._release()
        return 0


def calibrate(config_path: Union[str, Path], **kwargs) -> int:
    """Opens the beep test on a config and blocks until the window closes."""
    app = CalibrationApp(config_path, **kwargs)
    for warning in app.warnings:
        print(f"warning: {warning}", file=sys.stderr)
    return app.run()
