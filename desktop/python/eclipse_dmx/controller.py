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
from pathlib import Path
from typing import Callable, List, Optional, Sequence, Tuple, Union

from .binary import find_executable
from .config import Config, ConfigError

Color = Union[str, Sequence[float]]

#: One frame as the viewer sees it: the (r, g, b) each fixture is showing.
Frame = List[Tuple[int, int, int]]


class ShowError(RuntimeError):
    """Raised when the executable rejects a command or dies unexpectedly."""


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
        emit_frames: bool = False,
        emit_rate: float = 30.0,
        autostart: bool = True,
        command_timeout: float = 5.0,
    ) -> None:
        self.executable = find_executable(executable)
        self.dry_run = dry_run
        self.frames = frames
        self.verbose = verbose
        self.command_timeout = command_timeout

        # Asking for frames implies wanting them: a caller that passes on_frame
        # and forgets the flag would otherwise sit and watch nothing happen.
        self.emit_frames = emit_frames or on_frame is not None
        self.emit_rate = emit_rate

        self.on_log = on_log
        self.on_event = on_event
        self.on_frame = on_frame

        #: Fixture names, in patch order, as the executable reported them.
        self.fixture_names: List[str] = []

        #: States the running pattern offers. Empty unless it is a state
        #: machine, which is exactly the condition a UI wants to test.
        self.state_names: List[str] = []
        #: The state showing now, or "" when the pattern has no states.
        self.current_state: str = ""

        self._process: Optional[subprocess.Popen] = None
        self._replies: "queue.Queue[str]" = queue.Queue()
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
        if self.verbose:
            args.append("--verbose")
        return args

    # -- lifecycle --------------------------------------------------------

    def start(self) -> None:
        if self._process is not None:
            raise ShowError("this show is already running")

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
            failure = next((event for event in self._events if event.startswith("ERR")), None)
            if failure is not None:
                self.stop()
                raise ShowError(f"eclipse-dmx failed to start: {failure[4:]}")
            if self._process.poll() is not None:
                self.stop()
                raise ShowError(
                    f"eclipse-dmx exited immediately with code {self._process.returncode}"
                )
            time.sleep(0.02)

        self.stop()
        raise ShowError("eclipse-dmx did not report READY in time")

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

        # Frame lines arrive tens of times a second and are pure data, so they
        # are dispatched and dropped rather than kept in _events, which would
        # otherwise grow without bound for the length of the show.
        if line.startswith("F "):
            if self.on_frame is not None:
                frame = _parse_frame(line)
                if frame is not None:
                    self.on_frame(frame)
            return

        if line.startswith("FIXTURES "):
            self.fixture_names = line.split()[1:]

        # A state machine announces its looks when it starts and whenever the
        # pattern changes. A plain pattern announces an empty list, which is how
        # a UI knows to put its state buttons away.
        if line.startswith("STATES"):
            self.state_names = line.split()[1:]
        elif line.startswith("STATE "):
            self.current_state = line[len("STATE "):].strip()

        if line.startswith("OK") or line.startswith("ERR"):
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
