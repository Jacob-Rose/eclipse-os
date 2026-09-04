"""OSC in: Synesthesia's audio engine, driving this rig's knobs.

The other direction of `osc.py`. That one sends the rig's colour to the
visualiser; this one takes the visualiser's *analysis* back — bass, mids,
highs, transients, presence, its beat detector and its BPM — and binds it to
whatever the running look exposes.

Why take audio from a visualiser at all
---------------------------------------
Because it is already being computed, by something with a better view of the
music than this program has. eclipse-dmx hears nothing: its beat arrives as
MIDI notes from Mixxx (see `midi` in a config), which is a beat grid and a VU
meter and nothing else. Synesthesia is doing a real FFT on the same music and
publishing forty-odd derived values a frame — "how loud are the highs right
now", "did the bass just hit", "how confident is the tempo". Those are exactly
the numbers a look wants and the ones this rig has no way to compute.

So the wire goes both ways over the same protocol: colour out, analysis in,
neither end knowing what the other is for.

What arrives, and the one thing to check
----------------------------------------
Synesthesia Pro's **Settings > OSC > Output Audio Variables** sends its
`syn_*` uniforms — `syn_BassLevel`, `syn_Hits`, `syn_OnBeat`, `syn_BPM` and
the rest — to an address and port of your choosing.

**The OSC addresses those arrive on are not in Synesthesia's documentation**,
and they changed in v1.20 ("more nested and readable"). So the bindings below
match on a *pattern* rather than an exact address, and `osc-watch` prints what
this build of the app actually sends:

    python -m eclipse_dmx osc-watch --port 7000

That is the same move `midi-watch` makes for Mixxx's notes, for the same
reason: every step of wiring this up is verifiable except the last one, and
guessing at it is how a set opens with a look bound to nothing.

The shape of a binding
----------------------
A `Binding` is a pattern, a mode, and an action:

    pattern   which addresses it listens for - a glob, so `*BassLevel*`
              catches the address whatever the app nests it under
    exclude   globs that veto a match, because the useful patterns overlap:
              "the whole spectrum's level" is `*level*` minus the four bands,
              and a glob cannot say "not" on its own
    argument  which of the message's arguments carries the number
    mode      value    run on every message, carrying the scaled 0..1
              trigger  run once when it crosses up through `threshold`,
                       which is what a beat spike is
    action    a key into midi_map.ACTIONS, plus that action's parameters

Actions are shared with the MIDI mappings on purpose — the same registry, the
same `ActionContext`, the same files-are-presets shape. A pad and a bass drum
should be able to do the same things, and nothing in either dispatcher knows
what the other exists for.

Rate, and why it is not a detail
--------------------------------
Audio uniforms arrive at frame rate: sixty a second, per uniform, forever.
Every one of them turned into a protocol line would be thousands of lines a
minute down a pipe the desk also uses for cues. So a value binding is limited
two ways — a minimum interval between sends and a minimum change worth
sending — and both are per binding, because a level wants smoothing and a beat
must never be dropped.
"""

from __future__ import annotations

import fnmatch
import json
import socket
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple, Union

from .midi_map import ACTIONS, ActionContext, coerce_params
from .osc import decode

#: Where Synesthesia is pointed, unless its settings say otherwise.
#:
#: Its OSC panel has an input port and an output port and they are different
#: settings; this is the *output* one - what the app sends, what we listen to.
#: 7000 is this rig's convention rather than the app's default, and it is what
#: `launch-mythos-set.sh` and the docs use. The app must be told the same
#: number; nothing here can tell whether it was.
DEFAULT_INPUT_PORT = 7000

#: Bind address. Everything, rather than loopback, because the visualiser is
#: usually the machine *next* to this one - the mac mini running Synesthesia
#: sending to the laptop running the rig. Loopback would take only a sender on
#: this same machine and refuse the normal case in silence.
DEFAULT_BIND = "0.0.0.0"

MODES = ("value", "trigger")


# ---------------------------------------------------------------------------
# the socket
# ---------------------------------------------------------------------------

class OscListener:
    """A UDP port, decoded, on its own thread.

    Every error is swallowed and counted, for the reason the sender swallows
    its own: this is analysis driving decoration, and a malformed packet from
    something else on the network must not be able to interrupt a rig.
    """

    def __init__(
        self,
        port: int = DEFAULT_INPUT_PORT,
        host: str = DEFAULT_BIND,
        on_message: Optional[Callable[[str, List[object]], None]] = None,
    ) -> None:
        self.port = port
        self.host = host
        self.on_message = on_message

        #: Packets in, messages out of them, and packets that decoded to
        #: nothing. The third is the interesting one: it is what a sender
        #: speaking something other than OSC 1.0 looks like from here.
        self.packets = 0
        self.messages = 0
        self.undecodable = 0
        self.last_error: Optional[str] = None

        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self._socket.bind((host, port))
        except OSError:
            self._socket.close()
            raise

        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._read, name="osc-in", daemon=True)
        self._thread.start()

    def _read(self) -> None:
        # A timeout rather than a blocking read, so close() is not waiting on
        # the next packet from a visualiser that may already be shut.
        self._socket.settimeout(0.25)
        while not self._stop.is_set():
            try:
                packet, _sender = self._socket.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError as error:
                if self._stop.is_set():
                    return
                self.last_error = str(error)
                continue

            self.packets += 1
            try:
                messages = decode(packet)
            except Exception as error:      # decode() is written not to raise:
                self.undecodable += 1       # this is the belt to that brace
                self.last_error = str(error)
                continue

            if not messages:
                self.undecodable += 1
                continue

            for address, arguments in messages:
                self.messages += 1
                if self.on_message is not None:
                    try:
                        self.on_message(address, arguments)
                    except Exception as error:
                        # A binding that throws must not stop the port. The
                        # dispatcher fences each action itself; this catches
                        # whatever it could not.
                        self.last_error = f"{address}: {error}"

    def close(self) -> None:
        self._stop.set()
        self._socket.close()
        self._thread.join(timeout=0.5)

    def __enter__(self) -> "OscListener":
        return self

    def __exit__(self, *_exception) -> None:
        self.close()

    def describe(self) -> str:
        where = "every interface" if self.host in ("", "0.0.0.0") else self.host
        return f"listening on {where}:{self.port}"


# ---------------------------------------------------------------------------
# the binding
# ---------------------------------------------------------------------------

@dataclass
class Binding:
    """One incoming address, bound to one action."""

    label: str = "binding"
    pattern: str = "*"          # glob against the OSC address
    exclude: List[str] = field(default_factory=list)   # globs that veto a match
    argument: int = 0           # which argument carries the number
    mode: str = "value"         # value / trigger
    low: float = 0.0            # the incoming range, mapped onto 0..1
    high: float = 1.0
    threshold: float = 0.5      # trigger mode: what counts as "up"
    min_interval: float = 1 / 30.0
    min_change: float = 0.01
    action: str = "param"
    params: Dict[str, object] = field(default_factory=dict)
    enabled: bool = True

    # Runtime, not saved: the last value sent and when, which is what makes
    # the rate limit a rate limit and the trigger an edge rather than a level.
    _last_sent: float = field(default=-1.0, repr=False, compare=False)
    _last_at: float = field(default=0.0, repr=False, compare=False)
    _was_up: bool = field(default=False, repr=False, compare=False)

    # -- matching ----------------------------------------------------------

    def matches(self, address: str) -> bool:
        """Glob, case-insensitively, minus anything `exclude` claims.

        Case-insensitive because the one thing known about these addresses is
        that they are derived from names like `syn_BassLevel` and that the
        derivation changed once already. A binding that works until the app
        lowercases a segment is a binding that fails at a venue.

        `exclude` is there because the useful patterns overlap: the whole
        spectrum's level is "the one with 'level' in it that is not a band",
        and a glob cannot say "not" on its own. Without it, a binding meant for
        `syn_Level` quietly also takes `syn_BassLevel`, `syn_MidLevel` and
        `syn_HighLevel`, and one knob is driven by four sources at once.
        """
        folded = address.lower()
        if not fnmatch.fnmatch(folded, self.pattern.lower()):
            return False
        return not any(fnmatch.fnmatch(folded, veto.lower()) for veto in self.exclude)

    def scale(self, raw: float) -> float:
        """The incoming number, as 0..1 across [low, high], clamped.

        `low`/`high` are how a uniform with its own range - `syn_BPM` runs
        50..220 - reaches an action that speaks 0..1, without every action
        having to know about tempo.
        """
        span = self.high - self.low
        if span == 0:
            return 0.0
        return max(0.0, min(1.0, (raw - self.low) / span))

    def fires_on(self, raw: float, now: float) -> Optional[float]:
        """The value to run with, or None if this message should be dropped.

        Everything that decides *not* to act lives here: the rate limit, the
        change threshold, and the rising edge. The dispatcher just runs what
        this returns.
        """
        value = self.scale(raw)

        if self.mode == "trigger":
            up = value >= self.threshold
            fired = up and not self._was_up
            self._was_up = up
            if not fired:
                return None
            # A beat is never rate-limited or deduplicated: dropping one is
            # the whole failure this mode exists to avoid.
            self._last_sent = value
            self._last_at = now
            return 1.0

        if now - self._last_at < self.min_interval:
            return None
        if self._last_sent >= 0.0 and abs(value - self._last_sent) < self.min_change:
            # Not a rate limit but a silence limit: a level that is holding
            # still says nothing rather than restating itself thirty times a
            # second, which is what leaves the pipe clear for cues.
            return None

        self._last_sent = value
        self._last_at = now
        return value

    # -- the file ----------------------------------------------------------

    def to_dict(self) -> Dict[str, object]:
        return {
            "label": self.label,
            "trigger": {"pattern": self.pattern, "exclude": list(self.exclude),
                        "argument": self.argument},
            "mode": self.mode,
            "range": {"low": self.low, "high": self.high, "threshold": self.threshold},
            "limit": {"interval": self.min_interval, "change": self.min_change},
            "action": self.action,
            "params": dict(self.params),
            "enabled": self.enabled,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, object]) -> "Binding":
        trigger = data.get("trigger", {})
        span = data.get("range", {})
        limit = data.get("limit", {})

        binding = cls(
            label=str(data.get("label", "binding")),
            pattern=str(trigger.get("pattern", "*")),
            exclude=[str(veto) for veto in trigger.get("exclude", [])],
            argument=int(trigger.get("argument", 0)),
            mode=str(data.get("mode", "value")),
            low=float(span.get("low", 0.0)),
            high=float(span.get("high", 1.0)),
            threshold=float(span.get("threshold", 0.5)),
            min_interval=float(limit.get("interval", 1 / 30.0)),
            min_change=float(limit.get("change", 0.01)),
            action=str(data.get("action", "param")),
            params=dict(data.get("params", {})),
            enabled=bool(data.get("enabled", True)),
        )
        if binding.mode not in MODES:
            binding.mode = "value"
        spec = ACTIONS.get(binding.action)
        if spec is not None:
            binding.params = coerce_params(spec, binding.params)
        return binding

    def describe_trigger(self) -> str:
        argument = "" if self.argument == 0 else f"[{self.argument}]"
        veto = f" not {'/'.join(self.exclude)}" if self.exclude else ""
        return f"{self.pattern}{argument}{veto} {self.mode}"


class BindingSet:
    """The bindings, as a unit: what the audio engine drives tonight."""

    def __init__(self, bindings: Optional[List[Binding]] = None) -> None:
        self.bindings: List[Binding] = bindings or []

    def to_dict(self) -> Dict[str, object]:
        return {"version": 1, "bindings": [b.to_dict() for b in self.bindings]}

    @classmethod
    def from_dict(cls, data: Dict[str, object]) -> "BindingSet":
        return cls([Binding.from_dict(entry) for entry in data.get("bindings", [])])

    def save(self, path: Union[str, Path]) -> None:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_dict(), indent=2) + "\n", encoding="utf-8")

    @classmethod
    def load(cls, path: Union[str, Path]) -> "BindingSet":
        return cls.from_dict(json.loads(Path(path).read_text(encoding="utf-8")))


# ---------------------------------------------------------------------------
# firing
# ---------------------------------------------------------------------------

class Dispatcher:
    """Messages in, actions out. The OSC twin of midi_map.Dispatcher.

    Fenced the same way and for the same reason: one binding aimed at a knob
    the running look does not have must not stop the binding after it, and the
    look under a cue button changes all night.
    """

    def __init__(self, bindings: BindingSet, context: ActionContext) -> None:
        self.bindings = bindings
        self.context = context

        #: The last line each binding said, which is what keeps it from
        #: saying it again. Audio arrives at frame rate, so anything a binding
        #: has to say - a refusal, or a cue it just fired - it would otherwise
        #: say sixty times a second. A UI can read this as "what is each
        #: binding up to"; an entry that is a refusal is a binding aimed at
        #: nothing, which is the state worth being able to see.
        self.last_said: Dict[str, str] = {}

    def handle(self, address: str, arguments: Sequence[object]) -> List[str]:
        """Runs every enabled binding this message fires."""
        now = time.monotonic()
        said: List[str] = []

        # Before the bindings, and regardless of whether any of them wants
        # this address: Synesthesia announces `/scenes/{name}` when a scene is
        # launched, and that is the one thing the app tells us about itself
        # rather than about the music. It is what makes a scene pad's lamp
        # true instead of hopeful - and it catches a scene changed in the
        # app's own window, which nothing else here can see.
        self.context.syn.hear(address)

        for binding in self.bindings.bindings:
            if not binding.enabled or not binding.matches(address):
                continue

            raw = _number(arguments, binding.argument)
            if raw is None:
                continue

            value = binding.fires_on(raw, now)
            if value is None:
                continue

            spec = ACTIONS.get(binding.action)
            if spec is None:
                self.last_said[binding.label] = f"unknown action '{binding.action}'"
                continue

            try:
                line = spec.run(self.context, coerce_params(spec, binding.params), value)
            except Exception as error:
                self.last_said[binding.label] = str(error)
                continue

            if not line:
                self.last_said.pop(binding.label, None)
                continue

            # Said once, not thirty times a second. Whatever a binding has to
            # say repeats at the rate the audio arrives - a knob the running
            # look does not have is not news on every bass note - so a binding
            # saying what it said last time says nothing. It speaks again the
            # moment it has something different to say, including coming back
            # to working.
            if self.last_said.get(binding.label) != line:
                self.last_said[binding.label] = line
                said.append(line)

        return said


def _number(arguments: Sequence[object], index: int) -> Optional[float]:
    """The argument at `index` as a float, or None if there is no number there.

    A message whose argument is a string or is simply absent is not an error
    and not a zero: it is a message this binding has nothing to do with, and
    zero would be a value - a level of nothing, sent to a knob.
    """
    if index < 0 or index >= len(arguments):
        return None
    value = arguments[index]
    if isinstance(value, bool):
        return 1.0 if value else 0.0
    if isinstance(value, (int, float)):
        return float(value)
    return None
