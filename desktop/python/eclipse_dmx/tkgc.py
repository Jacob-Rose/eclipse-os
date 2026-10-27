"""Keeping tk's teardown on the thread that built it.

tk objects - a root, a widget, a `StringVar` - must be finalised on the thread
that created them. Off that thread a `Variable.__del__` prints "main thread is
not in main loop" and a dead interpreter brings the whole process down with
`Tcl_AsyncDelete: async handler deleted by the wrong thread`. On Linux that
mostly goes unnoticed; Windows Tcl enforces it.

Nothing in this package touches tk off the main thread on purpose. What does
it by accident is the cyclic garbage collector: it runs on whichever thread
happens to allocate when the threshold trips, and a viewer has several that
allocate all day - the show's stdout reader, the OSC listener, the pads pump.
A widget tree the viewer has already dropped (a knob panel rebuilt on a cue)
is a reference cycle, so it waits for that collector, and then dies wherever
the collector happens to be standing.

So while a tk root exists, automatic collection is off and the root's own
pump collects instead, on the main thread, often enough that nothing piles
up. `hold()` when a root is built, `collect_sometimes()` from its pump,
`release()` after it is destroyed. Nested roots (a probe, a dialog) count.

`join_workers()` is the other half: a worker thread that outlives the window
and holds the last reference to it would finalise it on exit, so a quit
waits, briefly, for the threads it started.
"""

from __future__ import annotations

import gc
import threading
import time
from typing import Iterable

#: How often the pump collects, in seconds. A pump turn is ~16ms; a second of
#: garbage from a viewer is a few knob panels, not a problem.
COLLECT_EVERY = 1.0

_lock = threading.Lock()
_depth = 0
_last_collect = 0.0


def hold() -> None:
    """A tk root exists from now: automatic collection is off until release()."""
    global _depth
    with _lock:
        if _depth == 0:
            gc.disable()
        _depth += 1


def release() -> None:
    """The root is destroyed. Collect here, on this thread, then let the
    collector back if this was the last root."""
    global _depth
    with _lock:
        _depth = max(0, _depth - 1)
        last = _depth == 0
    gc.collect()
    if last:
        gc.enable()


def collect_sometimes() -> None:
    """One collection per COLLECT_EVERY, from a pump on the owning thread."""
    global _last_collect
    now = time.monotonic()
    if now - _last_collect >= COLLECT_EVERY:
        _last_collect = now
        gc.collect()


def join_workers(names: Iterable[str], timeout: float = 2.0) -> None:
    """Waits, bounded, for every live thread whose name starts with one of
    `names`. Called from the thread that is about to drop the window."""
    deadline = time.monotonic() + timeout
    prefixes = tuple(names)
    for thread in threading.enumerate():
        if thread is threading.current_thread() or not thread.name.startswith(prefixes):
            continue
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        thread.join(timeout=remaining)


#: The threads a viewer or a calibrator starts, by the names they are given.
WORKER_NAMES = ("eclipse-dmx reader", "osc-in", "osc-resolve", "pads", "click", "ramp ")
