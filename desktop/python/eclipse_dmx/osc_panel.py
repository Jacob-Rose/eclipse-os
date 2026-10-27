"""The OSC link, watched: what is arriving, what is going out, and whether
anything is there.

A band down the right of the desk like the MIDI map editor, toggled from the
button band or [o]. It exists because every other part of this wire is
verifiable and this one was not: the addresses Synesthesia sends are
undocumented and were renamed once already, so a map is a set of *guesses*
until something checks them against what the app on this machine actually
sends - and from inside a running show, a binding aimed at an address that
never arrives looks exactly like a visualiser that is not switched on.

`osc-watch` answers the same question, but it cannot run while a show holds
the port, which is precisely when the question gets asked. This can.

Discovery rather than a fixed list
----------------------------------
Nothing here knows what addresses exist. Rows appear as messages land, in the
order first seen, and the same for the outbound side. That is the only honest
shape for it: a build of the app that nests its uniforms differently, a scene
publishing controls of its own, a firmware sending something nobody wrote down
- all of it shows up here without this file being edited, and a list written
from the documentation would have hidden exactly the case worth seeing.

What "connected" can and cannot mean
------------------------------------
It is UDP both ways, so there is no connection and nothing to report the
absence of one. This panel therefore never claims a link is up. It reports the
three things that are actually knowable, and they are deliberately separate:

  - **in**: when a message last arrived. Proof the app is running, sending, and
    pointed at this machine.
  - **out**: whether the endpoint resolves, and how many sends the socket took
    or refused. Not proof anything received them - a closed UDP port usually
    says nothing at all, and on some platforms says nothing ever.
  - **app**: the last `/scenes/{name}` heard. The one message the visualiser
    sends about *itself*, so it is the closest thing to a handshake there is.

A rig sending happily to an app that is not listening reads here as "out:
sending, in: nothing heard", which is the failure that costs an evening and
the one a single "connected" lamp would have hidden.
"""

from __future__ import annotations

import threading
import time
import tkinter as tk
from typing import Callable, Dict, List, Optional, Sequence, Tuple

# the viewer's palette - keep in step with viewer.py (importing it from there
# would be circular: the viewer imports this)
PANEL = "#14181d"
PANEL_EDGE = "#2a323b"
TEXT = "#c8d0d8"
TEXT_DIM = "#6b7783"
TEXT_WARN = "#e8a33d"
TEXT_GOOD = "#5fa85f"
TEXT_BAD = "#c05a5a"
BUTTON_BG = "#232a32"
BUTTON_BG_ACTIVE = "#3d6ea5"
BUTTON_FG = "#c8d0d8"
LIST_BG = "#0e1115"

FONT = ("Consolas", 9)
FONT_SMALL = ("Consolas", 8)

#: Addresses kept per direction. Generous - Synesthesia sends about forty
#: uniforms and a scene's own controls can add as many again - but bounded, so
#: something misconfigured into spraying unique addresses cannot grow this
#: without limit for the length of a set.
MAX_ROWS = 96

#: How long after its last message an address is drawn as gone quiet. Longer
#: than AudioLevel::kStaleAfter, so the rig gives up on a channel before this
#: panel says it has - a row still lit while the light it drives has faded is
#: the wrong way round for something being used to diagnose exactly that.
QUIET_AFTER = 1.5


class OscTraffic:
    """Every address seen, in each direction, with its latest value.

    Written from two threads that are not tk's - the OSC listener's for
    inbound, the show reader's for outbound - and read from tk on the pump. So
    everything is under one lock and `snapshot` hands back plain tuples: a
    panel that walked live dicts would tear on a dict resized mid-iteration by
    a message arriving, which is a crash that happens once an hour and never
    while anyone is looking.
    """

    def __init__(self, max_rows: int = MAX_ROWS) -> None:
        self.max_rows = max_rows
        self._lock = threading.Lock()

        # address -> [count, last value, last seen, first seen]
        self._in: Dict[str, list] = {}
        self._out: Dict[str, list] = {}

        self.packets_in = 0
        self.messages_in = 0
        self.last_in_at: Optional[float] = None

        self.sends = 0
        self.send_failures = 0
        self.last_out_at: Optional[float] = None

        #: The last `/scenes/{name}` the app announced, and when. The one
        #: message it sends about itself rather than about the music.
        self.scene: Optional[str] = None
        self.scene_at: Optional[float] = None

        #: Addresses dropped because the table was full, so the panel can say
        #: so rather than quietly showing a partial picture.
        self.overflowed = 0

    # -- writing, from the wire threads -----------------------------------

    def saw_in(self, address: str, values: Sequence[object]) -> None:
        now = time.monotonic()
        with self._lock:
            self.messages_in += 1
            self.last_in_at = now
            if address.lower().startswith("/scenes/"):
                self.scene = address
                self.scene_at = now
            self._record(self._in, address, values, now)

    def saw_out(self, address: str, values: Sequence[object], delivered: bool) -> None:
        now = time.monotonic()
        with self._lock:
            self.sends += 1
            if not delivered:
                self.send_failures += 1
            self.last_out_at = now
            self._record(self._out, address, values, now)

    def _record(self, table: Dict[str, list], address: str,
                values: Sequence[object], now: float) -> None:
        entry = table.get(address)
        if entry is None:
            if len(table) >= self.max_rows:
                self.overflowed += 1
                return
            table[address] = [1, tuple(values), now, now]
            return
        entry[0] += 1
        entry[1] = tuple(values)
        entry[2] = now

    # -- reading, from tk --------------------------------------------------

    def snapshot(self) -> dict:
        """A frozen copy. Rows in the order first seen, which is discovery
        order and stays put - sorting by rate or value would make the list
        jump under the cursor exactly while it is being read."""
        with self._lock:
            return {
                "in": [(a, e[0], e[1], e[2]) for a, e in
                       sorted(self._in.items(), key=lambda kv: kv[1][3])],
                "out": [(a, e[0], e[1], e[2]) for a, e in
                        sorted(self._out.items(), key=lambda kv: kv[1][3])],
                "messages_in": self.messages_in,
                "last_in_at": self.last_in_at,
                "sends": self.sends,
                "send_failures": self.send_failures,
                "last_out_at": self.last_out_at,
                "scene": self.scene,
                "scene_at": self.scene_at,
                "overflowed": self.overflowed,
            }

    def clear(self) -> None:
        """Forget every address, keeping the counters.

        For the moment after a setting is changed at the far end: the old rows
        are then a record of what used to arrive, which is worse than nothing
        when the question is what arrives *now*.
        """
        with self._lock:
            self._in.clear()
            self._out.clear()
            self.overflowed = 0


def _age(now: float, at: Optional[float]) -> str:
    if at is None:
        return "never"
    gap = now - at
    if gap < 1.0:
        return "now"
    if gap < 90.0:
        return f"{gap:.0f}s ago"
    return f"{gap / 60.0:.0f}m ago"


def _value_text(values: tuple) -> str:
    if not values:
        return "-"
    parts = []
    for value in values[:3]:
        if isinstance(value, bool):
            parts.append("on" if value else "off")
        elif isinstance(value, (int, float)):
            parts.append(f"{float(value):.3f}")
        else:
            parts.append(str(value)[:18])
    if len(values) > 3:
        parts.append("...")
    return " ".join(parts)


def _bar(values: tuple, width: int = 6) -> str:
    """A 0..1 value as blocks. Blank for anything that is not a number in
    range, because a bar drawn for a string or a BPM would be a lie about
    what the number means."""
    if not values or not isinstance(values[0], (int, float)) or isinstance(values[0], bool):
        return " " * width
    value = float(values[0])
    if not 0.0 <= value <= 1.0:
        return " " * width
    filled = int(round(value * width))
    return ("#" * filled) + ("." * (width - filled))


class OscPanel:
    """The band. Owns no socket and no state but the traffic it is shown."""

    def __init__(
        self,
        parent: tk.Misc,
        traffic: OscTraffic,
        *,
        in_port: Optional[int] = None,
        link_describe: Optional[Callable[[], str]] = None,
        link_stats: Optional[Callable[[], Tuple[int, int, Optional[str]]]] = None,
        bindings_for: Optional[Callable[[str], List[str]]] = None,
        on_status: Optional[Callable[[str], None]] = None,
    ) -> None:
        self.traffic = traffic
        self.in_port = in_port
        self.link_describe = link_describe
        self.link_stats = link_stats
        self.bindings_for = bindings_for or (lambda address: [])
        self.on_status = on_status or (lambda line: None)

        self.frame = tk.Frame(parent, bg=PANEL, highlightbackground=PANEL_EDGE,
                              highlightthickness=1, width=430)
        self.frame.pack_propagate(False)

        header = tk.Frame(self.frame, bg=PANEL)
        header.pack(fill="x", padx=6, pady=(6, 2))
        tk.Label(header, text="osc", bg=PANEL, fg=TEXT, font=("Consolas", 11)
                 ).pack(side="left")
        tk.Button(header, text="clear", font=FONT_SMALL, command=self._clear,
                  bg=BUTTON_BG, fg=BUTTON_FG, activebackground=BUTTON_BG_ACTIVE,
                  activeforeground=BUTTON_FG, relief="flat", padx=6,
                  highlightthickness=0, borderwidth=0).pack(side="right")

        self.view = tk.Text(self.frame, bg=LIST_BG, fg=TEXT, font=FONT_SMALL,
                            relief="flat", highlightthickness=0, borderwidth=0,
                            wrap="none", padx=6, pady=4, state="disabled",
                            cursor="arrow")
        self.view.pack(fill="both", expand=True, padx=6, pady=(0, 6))

        for name, colour in (("dim", TEXT_DIM), ("warn", TEXT_WARN),
                             ("good", TEXT_GOOD), ("bad", TEXT_BAD)):
            self.view.tag_configure(name, foreground=colour)

    def _clear(self) -> None:
        self.traffic.clear()
        self.on_status("osc: forgot every address; watching again")

    # -- drawing -----------------------------------------------------------

    def refresh(self) -> None:
        """Redraws from a snapshot. Called on the viewer's pump.

        The whole widget is rewritten each time rather than diffed. It is at
        most a couple of hundred short lines and it happens at pump rate on a
        panel that is usually folded away; a diff here would be a second
        implementation of the panel to keep in step with the first.
        """
        now = time.monotonic()
        state = self.traffic.snapshot()

        lines: List[Tuple[str, Optional[str]]] = []

        # -- in ------------------------------------------------------------
        where = f":{self.in_port}" if self.in_port else " (not listening)"
        lines.append((f"in  {where}", "dim"))
        if state["last_in_at"] is None:
            lines.append(("  nothing has ever arrived", "bad"))
            lines.append(("  Synesthesia > Settings > OSC: is OUTPUT on, is", "dim"))
            lines.append(("  'Output Audio Variables' ticked, and is its", "dim"))
            lines.append((f"  output port {self.in_port or 7000} pointed at this machine?", "dim"))
        else:
            quiet = (now - state["last_in_at"]) >= QUIET_AFTER
            lines.append((
                f"  {len(state['in'])} addresses, {state['messages_in']} messages, "
                f"heard {_age(now, state['last_in_at'])}",
                "warn" if quiet else "good"))

        for address, count, values, at in state["in"]:
            quiet = (now - at) >= QUIET_AFTER
            takes = self.bindings_for(address)
            if address.lower().startswith("/scenes/"):
                # Taken by the desk itself rather than by a binding: this is
                # what makes a scene pad's lamp true instead of hopeful, and
                # it is read whether or not any binding wants it. Saying
                # "nothing" here would be the one row on this panel that was
                # actively wrong.
                target = "-> app scene"
                tag = "good"
            elif not takes:
                target = "-> nothing"
                tag = "dim"
            elif len(takes) == 1:
                target = "-> " + takes[0]
                tag = None
            else:
                # Two bindings on one address is not a warning, it is the bug:
                # on a bus channel it means whichever spoke last wins, and
                # there is no reading of that anything could show.
                target = "-> CLASH " + ", ".join(takes)
                tag = "bad"
            if quiet:
                tag = "dim"
            name = address if len(address) <= 26 else "..." + address[-23:]
            lines.append((f"  {name:<26} {_bar(values)} {_value_text(values):<14} {target}",
                          tag))

        # -- out -----------------------------------------------------------
        lines.append(("", None))
        lines.append((f"out {self.link_describe() if self.link_describe else '(no link)'}",
                      "dim"))
        if self.link_stats is not None:
            sent, dropped, error = self.link_stats()
            # Said as two numbers and never as a verdict: a socket taking
            # every send proves nothing about the far end - see osc.py.
            tag = "bad" if (dropped and not sent) else ("warn" if dropped else "good")
            lines.append((f"  {sent} sent, {dropped} refused by the socket", tag))
            if error:
                lines.append((f"  last error: {error[:44]}", "warn"))
            lines.append(("  a socket that accepts a send proves nothing about", "dim"))
            lines.append(("  the far end; UDP has no reply. See 'app' below.", "dim"))

        for address, count, values, at in state["out"]:
            name = address if len(address) <= 26 else "..." + address[-23:]
            lines.append((f"  {name:<26} {_bar(values)} {_value_text(values):<14} x{count}",
                          None))

        # -- the app itself --------------------------------------------------
        lines.append(("", None))
        lines.append(("app", "dim"))
        if state["scene"] is None:
            lines.append(("  no scene announced", "dim"))
            lines.append(("  the app says /scenes/<name> when one is launched,", "dim"))
            lines.append(("  so this fills in the moment it does - and it is the", "dim"))
            lines.append(("  only thing it tells us about itself.", "dim"))
        else:
            lines.append((f"  scene {state['scene']}  ({_age(now, state['scene_at'])})",
                          "good"))

        if state["overflowed"]:
            lines.append(("", None))
            lines.append((f"{state['overflowed']} further addresses not shown "
                          f"(cap {self.traffic.max_rows})", "warn"))

        self.view.configure(state="normal")
        self.view.delete("1.0", "end")
        for text, tag in lines:
            self.view.insert("end", text + "\n", tag or ())
        self.view.configure(state="disabled")
