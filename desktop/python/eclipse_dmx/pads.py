"""The midi map without the window: pads to cues, lamps from the map.

The viewer owns three things a set needs from a controller - the map's
mappings fired off `MIDI-IN`, the lamps painted from the same map, and the
cue table those mappings carry - and `--headless` had none of them: it was
the show with no pads, which on a set flown from a Launchpad is no set.
This is the viewer's midi half with the window taken away, for `run` and
`osc`.

The shape is the viewer's, for the viewer's reason. Events arrive on the
executable's reader thread and are *queued* there, never fired: an action
that talks the protocol waits on a reply that the reader thread itself
delivers, so firing from the callback would deadlock the desk on its first
pad. A worker thread drains the queue and paints the lamps, at the rate the
viewer's pump did.
"""
from __future__ import annotations

import threading
from collections import deque
from pathlib import Path
from typing import Callable, Deque, Optional

from .launchpad import LampPainter, clear as lamp_clear, programmer_mode
from .midi_map import ActionContext, Dispatcher, MappingSet, SynesthesiaState, parse_midi_line

#: How often the queue is drained and the lamps repainted. The painter
#: diffs, so a repaint with nothing changed sends nothing.
PUMP_SECONDS = 1.0 / 30.0


class HeadlessPads:
    """A midi map running against a show, with no window.

    `midimap` is the file; missing or broken, the desk starts with an empty
    map and says so, the way the viewer does - a map that will not load must
    not refuse a set. `midi_out` names the controller to light, or is empty
    for no lamps. `osc_factory` is the mapping actions' link to the
    visualiser, built on first use; `say` is one line to wherever status
    goes.

    Built before the show so its `on_midi` can be handed to the controller,
    then `attach`ed once the show exists.
    """

    def __init__(self, midimap: Optional[str], config=None,
                 osc_factory: Optional[Callable[[], object]] = None,
                 say: Optional[Callable[[str], None]] = None,
                 syn: Optional[SynesthesiaState] = None,
                 midi_out: str = "") -> None:
        self.say = say or (lambda line: None)
        self.mappings = MappingSet()
        self.note = ""
        if midimap:
            path = Path(midimap)
            if path.exists():
                try:
                    self.mappings = MappingSet.load(path)
                except (OSError, ValueError, KeyError) as error:
                    self.note = f"midi map: {error}"
            else:
                self.note = f"midi map: {path.name} not found; running with no pads"

        self.context = ActionContext(show=None, osc_factory=osc_factory, say=self.say,
                                     syn=syn, config=config)
        self.context.page = self.mappings.opens_on
        self.dispatcher = Dispatcher(self.mappings, self.context)
        self.painter = LampPainter(self.mappings)
        self.midi_out = (midi_out or "").strip()

        self._events: Deque[object] = deque(maxlen=256)
        self._show = None
        self._lamps_open = False
        self._stop = threading.Event()
        self._worker: Optional[threading.Thread] = None

    # -- the executable's reader thread ------------------------------------

    def on_midi(self, payload: str) -> None:
        """A `MIDI-IN` line. Parsed and queued; nothing fires here."""
        event = parse_midi_line(payload)
        # Mixxx's notes are on this stream too; the map is not for them.
        if event is not None and event.is_for_the_map:
            self._events.append(event)

    # -- lifecycle ---------------------------------------------------------

    def attach(self, show) -> "HeadlessPads":
        self._show = show
        self.context.show = show
        return self

    def open(self) -> None:
        """Monitor on, lamps up, the pump running. Never fatal past the
        monitor: a controller that will not light is a set without lamps."""
        if self._show is None:
            raise RuntimeError("attach() a show before open()")
        if self.note:
            self.say(self.note)
        live = sum(1 for one in self.mappings.mappings if one.enabled)
        self.say(f"pads: {live} of {len(self.mappings.mappings)} mappings live"
                 + (f", opening on '{self.context.page}'" if self.context.page else ""))

        # The monitor is what makes the executable say MIDI-IN at all.
        self._show.midi_monitor(True)

        if self.midi_out:
            self._open_lamps(self.midi_out)

        self._stop.clear()
        self._worker = threading.Thread(target=self._pump, name="pads", daemon=True)
        self._worker.start()

    def close(self) -> None:
        self._stop.set()
        if self._worker is not None:
            self._worker.join(timeout=1.0)
            self._worker = None
        self._close_lamps()

    # -- the pump ----------------------------------------------------------

    def _pump(self) -> None:
        while not self._stop.wait(PUMP_SECONDS):
            self._drain()
            self._paint_lamps()

    def _drain(self) -> None:
        fired = []
        while self._events:
            event = self._events.popleft()
            try:
                fired.extend(self.dispatcher.handle(event))
            except Exception as error:  # one bad press must not stop the pump
                self.say(f"pads: {error}")
        if fired:
            self.say(fired[-1])

    # -- lamps -------------------------------------------------------------

    def _open_lamps(self, spec: str) -> None:
        show = self._show
        try:
            port = show.midi_out_open(spec)
        except Exception as error:
            self.say(f"lamps: {error}")
            return
        try:
            # Live mode does not accept host LED messages - see launchpad.py.
            show.midi_send(programmer_mode(True))
            for message in lamp_clear():
                show.midi_send(message)
        except Exception as error:
            self.say(f"lamps: {error}")
            self._quiet(show.midi_out_close)
            return
        self._lamps_open = True
        self.painter.forget()
        self.say(f"lamps: {port or spec}")

    def _paint_lamps(self) -> None:
        if not self._lamps_open:
            return
        try:
            for message in self.painter.frame(self.context):
                self._show.midi_send(message)
        except Exception as error:
            # One failed repaint closes the lamps rather than retrying thirty
            # times a second down the wire the beat arrives on.
            self._lamps_open = False
            self.say(f"lamps: {error}; lamps off")

    def _close_lamps(self) -> None:
        """Dark, and back to Live mode - Programmer mode disables the
        controller's own Setup button, so a desk that exits without putting
        it back leaves the box in a state its front panel cannot leave."""
        if not self._lamps_open or self._show is None:
            return
        self._lamps_open = False
        show = self._show
        for send in (
            lambda: [show.midi_send(m) for m in lamp_clear()],
            lambda: show.midi_send(programmer_mode(False)),
            show.midi_out_close,
        ):
            self._quiet(send)

    @staticmethod
    def _quiet(action) -> None:
        try:
            action()
        except Exception:
            pass
