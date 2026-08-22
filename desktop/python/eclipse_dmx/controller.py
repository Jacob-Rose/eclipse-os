"""Driving a running eclipse-dmx process.

The split: the executable owns frame timing and the wire, python owns the
config and the decisions. ShowController is the seam - it writes the config,
starts the process, and speaks the line protocol on its stdin.

Everything here is blocking-but-bounded. A command waits for its OK/ERR reply
with a timeout, so a wedged process surfaces as an exception rather than a
silent no-op on a rig full of lights.
"""

from __future__ import annotations

import queue
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Optional, Sequence, Tuple, Union

from .binary import find_executable
from .config import Config, ConfigError

Color = Union[str, Sequence[float]]

#: One frame as the viewer sees it: the (r, g, b) each fixture is showing.
Frame = List[Tuple[int, int, int]]


class ShowError(RuntimeError):
    """Raised when the executable rejects a command or dies unexpectedly."""


@dataclass
class DeviceSpan:
    """Which slice of a frame belongs to which device.

    A frame is one flat run of fixtures across every device in the environment,
    in order. This says where each device's run starts and how long it is, which
    is all a UI needs to draw them apart.
    """

    index: int
    name: str
    first: int
    count: int
    output: str = ""

    def slice(self, frame: Frame) -> Frame:
        """This device's fixtures out of a whole-show frame."""
        return frame[self.first:self.first + self.count]


class Param:
    """One tunable knob on the running look.

    The set is per *look*, not per pattern: switching a state machine's cue
    replaces it wholesale, and the executable re-announces it every time that
    happens. So a UI rebuilds its controls from this rather than holding on to
    them.

    `kind` is "f", "b" or "c". A bool and a colour still carry a range,
    pointless as it is for both, so a UI can read every param the same way and
    only branch on the widget it builds.

    `value` is a float for the first two and a `#rrggbb` string for a colour -
    the same spelling the config file and the frame stream use.
    """

    __slots__ = ("name", "kind", "value", "minimum", "maximum")

    def __init__(self, name: str, kind: str, value: Union[float, str],
                 minimum: float, maximum: float):
        self.name = name
        self.kind = kind
        self.value = value
        self.minimum = minimum
        self.maximum = maximum

    @property
    def is_bool(self) -> bool:
        return self.kind == "b"

    @property
    def is_color(self) -> bool:
        return self.kind == "c"

    def __repr__(self) -> str:
        if self.is_bool:
            return f"Param({self.name}={bool(self.value)})"
        if self.is_color:
            return f"Param({self.name}={self.value})"
        return f"Param({self.name}={self.value:g}, {self.minimum:g}..{self.maximum:g})"


def _parse_param(line: str) -> Optional[Param]:
    """Parses one ``PARAM name f 0.2 0 1`` line. None on anything malformed.

    A colour's value is the one field that is not a number - ``PARAM color c
    #ffffff 0 1`` - so the kind is read before the value rather than after.
    """
    parts = line.split()
    if len(parts) < 6:
        return None
    try:
        value: Union[float, str] = parts[3] if parts[2] == "c" else float(parts[3])
        return Param(parts[1], parts[2], value, float(parts[4]), float(parts[5]))
    except ValueError:
        return None


def _parse_frame(line: str) -> Optional[Frame]:
    """Parses one ``F rrggbb rrggbb ...`` line into per-fixture colours.

    Returns None on anything malformed. A viewer dropping a frame is a blink;
    a viewer raising out of a reader thread is a dead window, so this never
    throws on bad input.
    """
    frame: Frame = []
    for swatch in line.split()[1:]:
        if len(swatch) != 6:
            return None
        try:
            value = int(swatch, 16)
        except ValueError:
            return None
        frame.append(((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF))
    return frame


class ShowController:
    """Starts eclipse-dmx and drives it.

    Use it as a context manager so the process is always shut down, which also
    guarantees the rig gets its dark frame on the way out::

        with ShowController(config) as show:
            show.set_pattern("chase")
            show.wait(seconds=30)
    """

    def __init__(
        self,
        config: Union[Config, str, Path],
        executable: Optional[Union[str, Path]] = None,
        dry_run: bool = False,
        frames: int = 0,
        verbose: bool = False,
        on_log: Optional[Callable[[str], None]] = None,
        on_event: Optional[Callable[[str], None]] = None,
        on_frame: Optional[Callable[[Frame], None]] = None,
        on_beat: Optional[Callable[[int, float], None]] = None,
        on_midi: Optional[Callable[[str], None]] = None,
        emit_frames: bool = False,
        emit_rate: float = 30.0,
        midi: Optional[str] = None,
        bpm: Optional[float] = None,
        autostart: bool = True,
        command_timeout: float = 5.0,
    ) -> None:
        self.executable = find_executable(executable)
        self.dry_run = dry_run
        self.frames = frames
        self.verbose = verbose
        self.command_timeout = command_timeout

        # Both override the config's midi block. `midi=""` is the way to say
        # "open nothing", distinct from None meaning "whatever the config said".
        self.midi_port = midi
        self.start_bpm = bpm

        # Asking for frames implies wanting them: a caller that passes on_frame
        # and forgets the flag would otherwise sit and watch nothing happen.
        # Beat lines ride the same flag, so on_beat implies it too.
        self.emit_frames = emit_frames or on_frame is not None or on_beat is not None
        self.emit_rate = emit_rate

        self.on_log = on_log
        self.on_event = on_event
        self.on_frame = on_frame
        self.on_beat = on_beat
        self.on_midi = on_midi

        #: The last few `midi monitor` lines. Bounded, because a chatty mapping
        #: sends hundreds a second and a monitor left on is a monitor forgotten.
        self.midi_seen: "deque[str]" = deque(maxlen=200)

        #: Tempo the executable is running on, and the beat it is up to. Both
        #: only move when frames are being emitted; see emit_frames.
        self.bpm: float = 0.0
        self.beat: int = 0
        #: Where that tempo comes from: internal, midi_clock, midi_note, manual.
        self.beat_source: str = "internal"
        #: True while beats are arriving from outside rather than free-running.
        self.beat_locked: bool = False

        #: Fixture names, in patch order, as the executable reported them.
        self.fixture_names: List[str] = []

        #: One entry per device, in the order their fixtures appear in a frame.
        #: Filled from the DEVICE lines the executable sends before the first
        #: frame; a UI slices `Frame` with these.
        self.devices: List[DeviceSpan] = []

        #: Devices in the show that have no wire, as (name, reason). A rig that
        #: is not plugged in does not stop the show - it renders, it is in the
        #: frame stream, it is on screen, it just goes nowhere - so this is the
        #: only place a caller finds out, and a UI is expected to say so rather
        #: than let a dark truss look like a working one.
        self.offline_devices: List[Tuple[str, str]] = []

        #: States the running pattern offers. Empty unless it is a state
        #: machine, which is exactly the condition a UI wants to test.
        self.state_names: List[str] = []
        #: The state showing now, or "" when the pattern has no states.
        self.current_state: str = ""

        #: Knobs the running look offers, in registration order. Replaced
        #: wholesale whenever the pattern or the state changes, so a UI can
        #: watch `params_revision` and rebuild when it moves.
        self.params: List[Param] = []
        #: Bumped on every complete set. Cheaper for a UI to compare than the
        #: list itself, and unlike comparing names it also catches a look whose
        #: knobs are the same ones on a different object.
        self.params_revision: int = 0

        #: The set being read right now, or None between blocks.
        self._params_open: Optional[List[Param]] = None

        #: "pixels" or "cue" when this show drives a relic over USB. Tracked
        #: rather than asked for, because the executable only announces it in
        #: reply to a command.
        self.link_mode: str = "pixels"

        #: Lines a relic has sent up its cable, newest last. Bounded, like the
        #: rest of the event list: a show left running overnight must not grow
        #: a list per frame.
        self.relic_lines: List[str] = []

        self._process: Optional[subprocess.Popen] = None
        self._replies: "queue.Queue[str]" = queue.Queue()

        #: Whether READY has been seen. Only start() cares, and only to tell an
        #: ERR that is an answer from an ERR that is a refusal to start.
        self._ready: bool = False
        self._events: List[str] = []
        self._warnings: List[str] = []
        self._reader_threads: List[threading.Thread] = []
        self._temp_config: Optional[Path] = None

        self.config_path = self._prepare_config(config)

        if autostart:
            self.start()

    # -- setup ------------------------------------------------------------

    def _prepare_config(self, config: Union[Config, str, Path]) -> Path:
        """Materialises the config on disk, since the executable reads a file."""
        if isinstance(config, Config):
            warnings = config.validate()
            self._warnings.extend(warnings)

            handle = tempfile.NamedTemporaryFile(
                mode="w", suffix=".json", prefix="eclipse-dmx-", delete=False, encoding="utf-8"
            )
            with handle:
                handle.write(config.to_json())

            self._temp_config = Path(handle.name)
            return self._temp_config

        path = Path(config)
        if not path.exists():
            raise ConfigError(f"no config file at '{path}'")
        return path

    def _build_args(self) -> List[str]:
        args = [str(self.executable), "--config", str(self.config_path)]
        if self.dry_run:
            args.append("--dry-run")
        if self.frames > 0:
            args.extend(["--frames", str(self.frames)])
        if self.emit_frames:
            args.extend(["--emit-frames", "--emit-rate", str(self.emit_rate)])
        if self.midi_port is not None:
            args.extend(["--midi", self.midi_port] if self.midi_port else ["--no-midi"])
        if self.start_bpm is not None:
            args.extend(["--bpm", str(self.start_bpm)])
        if self.verbose:
            args.append("--verbose")
        return args

    # -- lifecycle --------------------------------------------------------

    def start(self) -> None:
        if self._process is not None:
            raise ShowError("this show is already running")

        self._ready = False
        self.offline_devices = []
        self._process = subprocess.Popen(
            self._build_args(),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )

        self._start_reader(self._process.stdout, self._handle_stdout)
        self._start_reader(self._process.stderr, self._handle_stderr)

        # Wait for READY (or an early ERR) so callers can assume the rig is
        # live the moment the constructor returns.
        deadline = time.monotonic() + max(self.command_timeout, 5.0)
        while time.monotonic() < deadline:
            if any(event.startswith("READY") for event in self._events):
                return
            failure = self._startup_failure()
            if failure is not None:
                self.stop()
                raise ShowError(f"eclipse-dmx failed to start: {failure}")

            if self._process.poll() is not None:
                # It is gone, but its last lines may still be in the reader
                # threads: a refusal to start is written and then exited on
                # immediately, so poll() routinely wins that race. Drain before
                # concluding it died without saying why - the reason is what the
                # caller needs, and the exit code alone is 1 for everything.
                for thread in self._reader_threads:
                    thread.join(timeout=0.5)

                failure = self._startup_failure()
                # stop() clears _process, so take the code from it rather than
                # reading the attribute back off a field that is now None.
                code = self.stop()
                if failure is not None:
                    raise ShowError(f"eclipse-dmx failed to start: {failure}")
                raise ShowError(f"eclipse-dmx exited immediately with code {code}")
            time.sleep(0.02)

        self.stop()
        raise ShowError("eclipse-dmx did not report READY in time")

    def _startup_failure(self) -> Optional[str]:
        """Why the show refused to start, or None while it still might.

        Reads _events rather than the reply queue: _handle_stdout copies a
        pre-READY ERR into both, so nothing has to be taken out of a queue a
        later command is entitled to read.
        """
        failure = next((event for event in self._events if event.startswith("ERR")), None)
        return None if failure is None else failure[len("ERR "):].strip()

    def _start_reader(self, stream, handler: Callable[[str], None]) -> None:
        thread = threading.Thread(target=self._pump, args=(stream, handler), daemon=True)
        thread.start()
        self._reader_threads.append(thread)

    @staticmethod
    def _pump(stream, handler: Callable[[str], None]) -> None:
        try:
            for line in stream:
                handler(line.rstrip("\r\n"))
        except (ValueError, OSError):
            # stream closed under us during shutdown; nothing to do
            pass

    def _handle_stdout(self, line: str) -> None:
        if not line:
            return

        # Any other line closes an open PARAMS block, and this has to happen
        # before the early returns below: a set is followed immediately by frame
        # lines, and leaving the block open would make the next lone PARAM echo
        # append a duplicate instead of updating in place.
        if not line.startswith("PARAM"):
            self._params_open = None

        # Frame lines arrive tens of times a second and are pure data, so they
        # are dispatched and dropped rather than kept in _events, which would
        # otherwise grow without bound for the length of the show.
        if line.startswith("F "):
            if self.on_frame is not None:
                frame = _parse_frame(line)
                if frame is not None:
                    self.on_frame(frame)
            return

        # Twice a second for the length of a show, so handled here with the
        # frames rather than appended to _events, for the same reason.
        if line.startswith("BEAT "):
            parts = line.split()
            try:
                self.beat = int(parts[1])
                self.bpm = float(parts[2])
            except (IndexError, ValueError):
                return
            if len(parts) > 3:
                self.beat_source = parts[3]
            if len(parts) > 4:
                self.beat_locked = parts[4] == "lock"
            if self.on_beat is not None:
                self.on_beat(self.beat, self.bpm)
            return

        # Also unbounded if left running, and for the same reason kept out of
        # _events: `midi monitor on` against a mapping sending VU meters is
        # hundreds of lines a second.
        if line.startswith("MIDI-IN "):
            payload = line[len("MIDI-IN "):]
            self.midi_seen.append(payload)
            if self.on_midi is not None:
                self.on_midi(payload)
            return

        # OFFLINE <device>: <reason>
        #
        # A device that came up without a wire. Deliberately not ERR: on this
        # protocol ERR is how a *command* is refused, and start() reads a
        # pre-READY ERR as a show that will not run - which this is not.
        if line.startswith("OFFLINE "):
            name, _, reason = line[len("OFFLINE "):].partition(":")
            self.offline_devices.append((name.strip(), reason.strip()))

        if line.startswith("FIXTURES "):
            self.fixture_names = line.split()[1:]

        # DEVICE <index> <name> <first fixture> <count> <output...>
        #
        # Sent once, before the first frame, so a UI can build a panel per
        # device and then slice every frame apart without being told again.
        if line.startswith("DEVICE "):
            parts = line.split(None, 5)
            if len(parts) >= 5:
                try:
                    self.devices.append(DeviceSpan(
                        index=int(parts[1]),
                        name=parts[2],
                        first=int(parts[3]),
                        count=int(parts[4]),
                        output=parts[5] if len(parts) > 5 else "",
                    ))
                except ValueError:
                    # A malformed announcement must not take the viewer down;
                    # the worst case is one device drawn with the rest.
                    pass

        # What a relic on the other end of a USB cable has to say: its answer to
        # a Hello, and a note each time a takeover starts or lapses. Kept
        # bounded for the same reason frame lines are not kept at all.
        if line.startswith("RELIC "):
            self.relic_lines.append(line[len("RELIC "):])
            del self.relic_lines[:-32]

        # A state machine announces its looks when it starts and whenever the
        # pattern changes. A plain pattern announces an empty list, which is how
        # a UI knows to put its state buttons away.
        if line.startswith("STATES"):
            self.state_names = line.split()[1:]
        elif line.startswith("STATE "):
            self.current_state = line[len("STATE "):].strip()

        # PARAMS opens a new set and PARAM lines fill it. Collected here rather
        # than by asking, because the set changes under a UI whenever the cue
        # does and a UI that only reads on demand shows knobs for a look that is
        # no longer running.
        if line.startswith("PARAMS"):
            self._params_open = []
            self.params = self._params_open
            self.params_revision += 1
        elif line.startswith("PARAM "):
            param = _parse_param(line)
            if param is None:
                pass
            elif self._params_open is not None:
                self._params_open.append(param)
            else:
                # A lone PARAM is the echo after a `param` command, carrying
                # what the value actually landed on once clamped. Updated in
                # place so a UI does not rebuild every time a slider moves.
                for index, existing in enumerate(self.params):
                    if existing.name == param.name:
                        self.params[index] = param
                        break

        if line.startswith("READY"):
            self._ready = True

        if line.startswith("OK") or line.startswith("ERR"):
            # Before READY nothing has been asked, so an ERR here is not an
            # answer waiting to be collected - it is the show saying why it will
            # not start, and it is the only place that reason appears. Put it in
            # _events as well, or start() waits out a READY that is never coming
            # and reports an exit code in place of "no relic found".
            if not self._ready and line.startswith("ERR"):
                self._events.append(line)
            self._replies.put(line)
        else:
            self._events.append(line)
            if line.startswith("WARN "):
                self._warnings.append(line[len("WARN "):])

        if self.on_event is not None:
            self.on_event(line)

    def _handle_stderr(self, line: str) -> None:
        if self.on_log is not None:
            self.on_log(line)

    def stop(self, timeout: float = 5.0) -> Optional[int]:
        """Asks the show to quit, then makes sure it did."""
        process = self._process
        if process is None:
            return None

        if process.poll() is None:
            try:
                self._write_line("quit")
                process.wait(timeout=timeout)
            except (ShowError, subprocess.TimeoutExpired, OSError):
                process.terminate()
                try:
                    process.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=timeout)

        # Close the pipes rather than waiting for the collector. A long-lived
        # caller that starts a show per cue would otherwise leak three handles
        # each time, and on Windows that is a finite budget.
        for stream in (process.stdin, process.stdout, process.stderr):
            if stream is not None:
                try:
                    stream.close()
                except (OSError, ValueError):
                    pass

        returncode = process.returncode
        self._process = None
        self._cleanup_temp_config()
        return returncode

    def _cleanup_temp_config(self) -> None:
        if self._temp_config is not None:
            try:
                self._temp_config.unlink()
            except OSError:
                pass
            self._temp_config = None

    def __enter__(self) -> "ShowController":
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.stop()

    # -- state ------------------------------------------------------------

    @property
    def is_running(self) -> bool:
        return self._process is not None and self._process.poll() is None

    @property
    def warnings(self) -> List[str]:
        """Config warnings the executable reported at load."""
        return list(self._warnings)

    @property
    def events(self) -> List[str]:
        """Every non-reply line the executable has emitted."""
        return list(self._events)

    # -- the protocol -----------------------------------------------------

    def _write_line(self, line: str) -> None:
        process = self._process
        if process is None or process.stdin is None or process.poll() is not None:
            raise ShowError("the show is not running")

        try:
            process.stdin.write(line + "\n")
            process.stdin.flush()
        except (BrokenPipeError, OSError) as error:
            raise ShowError(f"lost the connection to eclipse-dmx: {error}") from error

    def command(self, line: str, expect_reply: bool = True) -> str:
        """Sends one raw protocol line and returns the reply.

        Raises ShowError on an ERR reply, so callers can treat a returned value
        as success without checking.
        """
        # Drop stale replies so a timed-out earlier command cannot be mistaken
        # for this one's answer.
        while True:
            try:
                self._replies.get_nowait()
            except queue.Empty:
                break

        self._write_line(line)

        if not expect_reply:
            return ""

        try:
            reply = self._replies.get(timeout=self.command_timeout)
        except queue.Empty as error:
            raise ShowError(f"no reply to '{line}' within {self.command_timeout}s") from error

        if reply.startswith("ERR"):
            raise ShowError(reply[4:].strip())
        return reply

    def set_pattern(self, name: str) -> None:
        """Switches the look. Speed/width/brightness carry across."""
        self.command(f"pattern {name}")

    def set_state(self, name: str) -> None:
        """Cross-fades a state machine pattern to `name`.

        Raises ShowError if the running pattern has no states, or no such one.
        """
        self.command(f"state {name}")
        self.current_state = name

    def set_input(self, channel: str, down: bool) -> None:
        """Drives one of the two momentary inputs a relic look can read.

        On a jacket these are its remote buttons. Here they are whatever a UI
        wires them to, which is how a look that was built around a button press
        stays expressive on a rig that has none.
        """
        self.command(f"input {channel} {'on' if down else 'off'}")

    def set_param(self, name: str, value: Union[float, bool, str]) -> None:
        """Turns one of the running look's knobs.

        A colour goes as `"#rrggbb"`; everything else as a number.

        Values outside a param's range are clamped rather than refused, and a
        rate snaps to the nearest musical one, so what you sent and what the
        look took are not always the same thing. The executable echoes back what
        it landed on, which is why `self.params` is right afterwards either way.
        """
        if isinstance(value, bool):
            value = 1 if value else 0
        self.command(f"param {name} {value}")

    def get_param(self, name: str) -> Optional[Param]:
        """The named knob on the running look, or None if it has no such one."""
        return next((param for param in self.params if param.name == name), None)

    def refresh_params(self) -> List[Param]:
        """Asks for the set outright, rather than waiting to be told.

        Rarely needed - the executable announces it on every pattern and state
        change - but a client that attached late has missed those.
        """
        self.command("params")
        return self.params

    def dump_params(self) -> str:
        """The running look's knobs as one line of JSON, for keeping.

        Tuning is live only: nothing is written to a config file behind you.
        This is what a tuning session leaves behind, and where it ends up is
        your decision.
        """
        self.command("params dump")
        dump = next((event for event in reversed(self._events) if event.startswith("DUMP ")), None)
        return dump[len("DUMP "):] if dump else "{}"

    def set_speed(self, value: float) -> None:
        self.command(f"speed {value}")

    def set_width(self, value: float) -> None:
        self.command(f"width {value}")

    def set_brightness(self, value: float) -> None:
        """Pattern brightness, 0..1."""
        self.command(f"brightness {value}")

    def set_master(self, value: float) -> None:
        """Rig-wide brightness, 0..1."""
        self.command(f"master {value}")

    def set_color(self, color: Color) -> None:
        """Colour for solid and pulse. Hex string, or an (h, s, v) triple."""
        if isinstance(color, str):
            self.command(f"color {color}")
        else:
            h, s, v = color
            self.command(f"color {h} {s} {v}")

    def set_palette(self, palette: Union[str, Sequence[str]]) -> None:
        """A built-in palette name, or a list of hex stops."""
        if isinstance(palette, str):
            self.command(f"palette {palette}")
        else:
            self.command("palette " + ",".join(palette))

    # -- tempo ------------------------------------------------------------
    #
    # The beat clock is process-wide rather than owned by a pattern, so these
    # work whatever is running: dial the tempo in on any look and it is already
    # right when you switch to one that uses it.

    def set_bpm(self, value: float) -> None:
        """Sets the tempo by hand, and stops following anything external."""
        self.command(f"bpm {value}")

    def tap_beat(self) -> None:
        """A downbeat, now.

        Two jobs: tapping a tempo in when there is no MIDI, and telling a clock
        that only sends 0xF8 where the bar actually starts.
        """
        self.command("beat")

    def midi_open(self, port: str = "auto") -> None:
        """Follows tempo from a MIDI input. `port` is an index, a name, a
        fragment of one, or "auto"."""
        self.command(f"midi open {port}")

    def midi_close(self) -> None:
        """Stops following. The tempo stays where it was."""
        self.command("midi close")

    def midi_align(self) -> None:
        """Declares that now is the downbeat, without changing the tempo."""
        self.command("midi align")

    def set_free_run(self, enable: bool = True) -> None:
        """Whether the rig keeps pulsing after the external clock stops."""
        self.command(f"midi free-run {'on' if enable else 'off'}")

    def set_link_mode(self, mode: str) -> None:
        """`pixels` to drive a relic's LEDs from here, `cue` to give them back.

        Raises ShowError when the show is not on a relic link at all, which is
        the honest answer: there is nothing to hand over.
        """
        if mode not in ("pixels", "cue"):
            raise ShowError(f"link mode must be 'pixels' or 'cue', got '{mode}'")
        self.command(f"link {mode}")
        self.link_mode = mode

    def release_link(self) -> None:
        """Hands a relic its own pixels back now, without leaving pixel mode."""
        self.command("link release")

    def midi_monitor(self, enable: bool = True) -> None:
        """Reports every channel message arriving, into `midi_seen`.

        This is how you find out what your DJ software actually sends rather
        than trusting its documentation. Turn it off again when done: a mapping
        with VU meters on emits hundreds of lines a second.
        """
        self.command(f"midi monitor {'on' if enable else 'off'}")

    def midi_status(self) -> str:
        """Port, filters, counts and the beat clock, as one line."""
        self._write_line("midi status")

        deadline = time.monotonic() + self.command_timeout
        seen = len(self._events)
        while time.monotonic() < deadline:
            for event in self._events[seen:]:
                if event.startswith("MIDI-STATUS"):
                    return event
            seen = len(self._events)
            time.sleep(0.02)

        raise ShowError("no midi status reply in time")

    def blackout(self, enable: bool = True) -> None:
        """Holds the rig dark without losing the running look."""
        self.command(f"blackout {'on' if enable else 'off'}")

    def status(self) -> str:
        """Current state, as reported by the executable."""
        self._write_line("status")

        deadline = time.monotonic() + self.command_timeout
        seen = len(self._events)
        while time.monotonic() < deadline:
            for event in self._events[seen:]:
                if event.startswith("STATUS"):
                    return event
            seen = len(self._events)
            time.sleep(0.02)

        raise ShowError("no status reply in time")

    # -- waiting ----------------------------------------------------------

    def wait(self, seconds: Optional[float] = None) -> Optional[int]:
        """Blocks. With `seconds`, for that long; without, until the show ends.

        Returns the exit code when the process finished, None on a timed wait
        that is still running.
        """
        process = self._process
        if process is None:
            return None

        try:
            return process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            return None


def run_config(
    config: Union[Config, str, Path],
    seconds: Optional[float] = None,
    dry_run: bool = False,
    echo_logs: bool = True,
) -> int:
    """Runs a config to completion. The one-liner for simple shows."""
    on_log = (lambda line: print(line, file=sys.stderr)) if echo_logs else None

    with ShowController(config, dry_run=dry_run, on_log=on_log) as show:
        for warning in show.warnings:
            print(f"warning: {warning}", file=sys.stderr)
        code = show.wait(seconds)
        return 0 if code is None else code
