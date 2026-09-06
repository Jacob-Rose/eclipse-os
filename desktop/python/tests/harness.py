"""Shared scaffolding for the suite: what to skip on, and how to wait.

Two problems, both of which every test class used to solve for itself.

**What to skip on.** Most of this suite drives the real executable, and a good
half of it opens a real window - the viewer's assertions read geometry and
pixel colour back off a live tk canvas, which only exists once a window manager
has mapped it. Neither is available on every machine the suite runs on, so both
are probed and skipped rather than assumed. The probe is done once per run and
remembered: opening and closing a window to find out whether windows open is
cheap, but nine of them is nine flashes on somebody's desktop.

**How to wait.** A viewer test cannot assert the instant it acts. The frame it
is waiting for comes off a subprocess, through a reader thread, into a tk
`after` pump - so there is always a gap, and it is never the same length twice.
The suite's answer was `settle(seconds)`, which burns its whole argument every
time. Every one of those numbers was picked as a worst case and then paid on
every run, which is most of where eight minutes went.

`settle_until(predicate)` is the same wait with an exit: it pumps until the
thing being waited for has happened and returns then, and only spends the full
timeout when the test is about to fail anyway. Use it wherever the predicate
can be the assertion itself. `settle(seconds)` is still right for the other
case - letting a fixed span of show time pass, where there is no event to wait
for and arriving early would mean measuring the wrong moment.

**On window managers.** These windows announce themselves under
``ECLIPSE_DMX_WM_CLASS`` (see `eclipse_dmx.viewer.WM_CLASS`) so a tiling
compositor can tell a test run from somebody's actual viewer and put the run
somewhere harmless. Without a rule they map onto whatever workspace is in
front, one after another, taking focus with them. The readme has the hyprland
rules; other compositors want the same three ideas - a workspace of its own,
floating, no focus.

Floating is not only politeness: a tiled window ignores the size a test asked
for, so the geometry tests were all quietly running at one tile size.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time
import unittest
from pathlib import Path

DESKTOP = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DESKTOP / "python"))

from eclipse_dmx.binary import BinaryNotFoundError, find_executable  # noqa: E402
from eclipse_dmx.controller import ShowController  # noqa: E402

#: The WM class a test run's windows carry. tkinter title-cases it, so a
#: compositor matches on "Eclipse-dmx-tests". Set before any window is built,
#: and `setdefault` so a run can be pointed elsewhere from the environment.
WM_CLASS = "eclipse-dmx-tests"
os.environ.setdefault("ECLIPSE_DMX_WM_CLASS", WM_CLASS)

#: Gap between pumps of the tk event loop. Slightly longer than tk's own 16ms
#: frame, so a pump loop costs one repaint rather than spinning on a busy wait.
TICK = 0.02

#: Default ceiling on a `settle_until`. Long enough for the executable to come
#: up, hand over a frame and have it painted, on a machine doing other things.
#: A test that needs longer says so; a test that hits this is failing.
PATIENCE = 4.0

_executable = None  # None until probed; then a Path, or the reason there is not one.
_display = None     # None until probed; then "" for yes, or the reason for no.


def executable_or_skip():
    """The built `eclipse-dmx`, or skip. Probed once - the search hits disk."""
    global _executable
    if _executable is None:
        try:
            _executable = find_executable()
        except BinaryNotFoundError as error:
            _executable = f"eclipse-dmx is not built: {error}"
    if isinstance(_executable, str):
        raise unittest.SkipTest(_executable)
    return _executable


def display_or_skip():
    """Skip unless there is a window manager that will map a window."""
    global _display
    if _display is None:
        try:
            import tkinter
        except ImportError as error:
            _display = f"no tkinter: {error}"
        else:
            try:
                tkinter.Tk(className=WM_CLASS).destroy()
            except Exception as error:
                _display = f"no display: {error}"
            else:
                _display = ""
    if _display:
        raise unittest.SkipTest(_display)


#: Frames to render when a test only wants the executable's answer to a
#: command rather than a picture. The whole piped script is drained before the
#: first frame, so the answer is already printed by then - but a frame's output
#: does not reach the pipe until the next one, so one frame is not enough. Two
#: is; four is two with room on a loaded machine. This used to be sixty, which
#: at 30fps is two seconds of rendering that nothing read, paid on every one of
#: these tests.
ANSWER_FRAMES = 4


def run_show(script, frames=ANSWER_FRAMES, config=None, timeout=60):
    """Run the show on `script` and hand back every line it printed.

    `ShowController.command` returns only the OK line, because that is what a
    caller waiting on a command wants. These assertions are about the
    announcement lines *around* it - CHANNEL, MOD, STATES - so this reads the
    stream directly rather than through the wrapper.

    Everything is written at once, so it all lands before the first frame
    renders. That is fine for anything the command handler answers on the spot,
    and wrong for anything a frame has to run to produce - for that, ask for
    the frames explicitly, or use a paced run that leaves gaps between lines.
    """
    result = subprocess.run(
        [str(executable_or_skip()), "--config", str(config), "--dry-run",
         "--midi", "", "--frames", str(frames)],
        input="\n".join(script) + "\n",
        capture_output=True, text=True, timeout=timeout,
    )
    return result.stdout.splitlines()


def run_show_paced(script, pause=0.05, config=None, timeout=30):
    """The same, with frames actually rendering between the lines.

    Some state is read by the *frame loop*, not by the command handler: an
    `audio` line only reaches the beat clock when a frame runs after it. So
    anything asking what the bus did has to leave room for one, which piping
    the whole script in at once does not.
    """
    process = subprocess.Popen(
        [str(executable_or_skip()), "--config", str(config), "--dry-run",
         "--midi", "", "--frames", "0"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        text=True,
    )
    try:
        for line in script:
            process.stdin.write(line + "\n")
            process.stdin.flush()
            time.sleep(pause)
        process.stdin.write("quit\n")
        process.stdin.flush()
        out, _ = process.communicate(timeout=timeout)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
    return out.splitlines()


class ShowTest(unittest.TestCase):
    """A test that drives the real executable but opens no window.

    Subclasses that set `self.app` get its event loop pumped while they wait;
    the ones that only listen to a controller get the same waits without one.
    """

    #: The built executable, for the tests that run it themselves rather than
    #: through a controller.
    executable = None

    @classmethod
    def setUpClass(cls):
        cls.executable = executable_or_skip()

    # -- the show under test ------------------------------------------------

    def running_show(self, config, **kwargs):
        """A `ShowController`, dry-run by default, stopped when the test ends.

        Every test that wants one used to open it inside its own try/finally.
        `addCleanup` is that try/finally, said once - and it still runs when an
        assertion raises, which is the case the hand-written ones were there
        for. Frames are dropped unless the caller asks for them.
        """
        kwargs.setdefault("on_frame", lambda frame: None)
        kwargs.setdefault("dry_run", True)
        show = ShowController(config, **kwargs)
        self.addCleanup(self._stop_quietly, show)
        return show

    def until_knobs(self, show, names, since=None, timeout=PATIENCE):
        """Wait until `show` has *finished* announcing exactly these knobs.

        A cue's knobs arrive as a block and the list fills in live as the lines
        land, so a test that watched the names alone would act on a half-filled
        list - and, when the new cue offers the same knobs as the one it
        replaced, on the *old* cue's block, which matches from the start.

        `params_revision` is what settles both: it only moves when a block
        closes. `since` is the revision from before whatever triggered the new
        one - pass it, or this cannot tell the new announcement from the old.
        """
        names = list(names)
        if since is None:
            since = show.params_revision

        def landed():
            return (show.params_revision > since
                    and [param.name for param in show.params] == names)

        return self.settle_until(
            landed, timeout,
            lambda: f"the cue's knobs to settle to {names} "
                    f"(now {[param.name for param in show.params]}, "
                    f"revision {show.params_revision})")

    @staticmethod
    def _stop_quietly(show):
        """Stop a show without letting the teardown bury the real failure."""
        try:
            show.stop()
        except Exception:
            pass

    # -- waiting ----------------------------------------------------------

    def pump(self):
        """One turn of the event loop, if this test has a window."""
        app = getattr(self, "app", None)
        if app is not None:
            app.root.update()

    def settle(self, seconds):
        """Let `seconds` of show time pass, pumping throughout.

        For waits with no event to catch - a cross-fade running its course, a
        beat arriving on its own schedule. When there *is* something to wait
        for, `settle_until` says so and costs less.
        """
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.pump()
            time.sleep(TICK)

    def settle_until(self, predicate, timeout=PATIENCE, message=None):
        """Pump until `predicate()` is true; fail if it never is.

        Returns as soon as the condition holds, so a wait sized for the worst
        case costs the typical one. `message` is what the failure says the
        suite was waiting for - write it as the thing that did not happen, and
        pass a callable if it should report the state the wait reached.
        """
        end = time.monotonic() + timeout
        while True:
            self.pump()
            result = predicate()
            if result:
                return result
            if time.monotonic() >= end:
                said = message() if callable(message) else message
                raise self.failureException(
                    f"timed out after {timeout:g}s waiting for "
                    f"{said or 'the condition'}")
            time.sleep(TICK)

    def settle_for_frames(self, count, timeout=PATIENCE):
        """Wait until `count` frames have arrived on `self.frames`."""
        return self.settle_until(
            lambda: len(self.frames) >= count, timeout,
            lambda: f"{count} frames from the executable (got {len(self.frames)})")


class GuiTest(ShowTest):
    """A `ShowTest` that also needs a window manager."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        display_or_skip()
