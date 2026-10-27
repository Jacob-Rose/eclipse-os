"""OSC in: Synesthesia's audio engine, driving this rig's knobs.

The other direction of `osc.py`. That one sends the rig's colour to the
visualiser; this one takes the visualiser's *analysis* back - bass, mids,
highs, transients, presence, its beat detector and its BPM - and binds it to
whatever the running look exposes.

Why take audio from a visualiser at all
---------------------------------------
Because it is already being computed, by something with a better view of the
music than this program has. eclipse-dmx hears nothing: its beat arrives as
MIDI notes from Mixxx (see `midi` in a config), which is a beat grid and a VU
meter and nothing else. Synesthesia is doing a real FFT on the same music and
publishing forty-odd derived values a frame - "how loud are the highs right
now", "did the bass just hit", "how confident is the tempo". Those are exactly
the numbers a look wants and the ones this rig has no way to compute.

Where the work happens
----------------------
In the executable. The socket, the packet decoder and the bindings are the
library's (`enet`, `eosc`), and the map file below is loaded there into
eosc binding sets. A binding whose action is `audio` lands on the bus as the
message arrives, and one whose action is `param` (with a `name`) lands on the
running look's knob - or `layer`'s look - once a frame, each in the knob's own
range (ecore::landUnit), with no pipe in the way. Everything else that fires
comes back as one line, ``OSC-FIRE "label" value``, and the `Dispatcher` here
runs the action the map named - the same registry as the MIDI mappings, on
purpose. A pad and a bass drum should be able to do the same things.

What arrives, and the one thing to check
----------------------------------------
Synesthesia Pro's **Settings > OSC > Output Audio Variables** sends its
`syn_*` uniforms to an address and port of your choosing. The OSC addresses
they arrive on are not in Synesthesia's documentation, and they changed in
v1.20. So a binding matches on a *glob* rather than an exact address, and
`osc-watch` prints what this build of the app actually sends.

The shape of a binding
----------------------
A `Binding` is a pattern, a mode, and an action:

    pattern   which addresses it listens for - a glob, so `*BassLevel*`
              catches the address whatever the app nests it under
    exclude   globs that veto a match, because the useful patterns overlap:
              "the whole spectrum's level" is `*level*` minus the four bands
    argument  which of the message's arguments carries the number
    mode      value    acts on every message, carrying the scaled 0..1
              trigger  acts once when it crosses up through `threshold`
    action    a key into midi_map.ACTIONS, plus that action's parameters

A value binding is limited two ways - a minimum interval between acts and a
minimum change worth acting on - with a keepalive so a held value is still
restated, because the bus fades a channel nobody has restated. Both are per
binding, because a level wants smoothing and a beat must never be dropped.
The limiting runs in the executable (ecore::BindingRule); the numbers live in
the file.
"""

from __future__ import annotations

import fnmatch
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from .controller import ShowError
from .midi_map import ACTIONS, ActionContext, coerce_params
from .osc import parse_status

DEFAULT_INPUT_PORT = 7000
DEFAULT_BIND = "0.0.0.0"
MODES = ("value", "trigger")


class OscListener:
    """The executable's OSC input, opened on a port with a map loaded.

    Every error on the wire is swallowed and counted there, for the reason the
    sender swallows its own: this is analysis driving decoration, and a
    malformed packet from something else on the network must not be able to
    interrupt a rig. A port that cannot be opened is the one failure that is
    raised, as OSError, since nothing will ever arrive.
    """

    def __init__(
        self,
        show,
        port: int = DEFAULT_INPUT_PORT,
        host: str = DEFAULT_BIND,
        map_path: Optional[Union[str, Path]] = None,
    ) -> None:
        self.show = show
        self.host = host
        self.port = port
        self.bindings_loaded = 0
        self.native = 0
        self.packets = 0
        self.messages = 0
        self.undecodable = 0
        self.fires = 0
        self.heard_ago: Optional[float] = None
        self.last_error: Optional[str] = None
        try:
            reply = show.command(f"osc listen {host}:{int(port)}")
        except ShowError as error:
            raise OSError(str(error)) from None
        # `OK osc listen 0.0.0.0:7000`: the port actually bound, for port 0
        try:
            self.port = int(reply.rsplit(":", 1)[1])
        except (IndexError, ValueError):
            pass
        if map_path is not None:
            self.load_map(map_path)

    def load_map(self, path: Union[str, Path]) -> Tuple[int, int]:
        """Loads a map file in the executable. Returns (bindings, native)."""
        reply = self.show.command(f"osc map {path}")
        words = reply.split()
        try:
            self.bindings_loaded = int(words[3])
            self.native = int(words[5])
        except (IndexError, ValueError):
            pass
        return self.bindings_loaded, self.native

    def announce(self, glob: str) -> None:
        """Have every message matching `glob` reported as an OSC-IN line,
        whatever the watch setting - a scene announcement, say."""
        self.show.command(f"osc announce {glob}")

    def watch(self, enable: bool = True) -> None:
        """Report every message as an OSC-IN line. Forty uniforms a frame:
        for looking, not for leaving on."""
        self.show.command(f"osc watch {'on' if enable else 'off'}")

    def refresh(self) -> Dict[str, str]:
        """Counts and the last-heard age from `osc status`."""
        for line in self.show.query("osc status", "OSC-STATUS"):
            fields = parse_status(line)
            self.packets = int(fields.get("packets", 0) or 0)
            self.messages = int(fields.get("messages", 0) or 0)
            self.undecodable = int(fields.get("undecodable", 0) or 0)
            self.fires = int(fields.get("fires", 0) or 0)
            heard = fields.get("heard", "never")
            self.heard_ago = None if heard == "never" else float(heard.rstrip("s"))
            return fields
        return {}

    def close(self) -> None:
        try:
            self.show.command("osc listen off")
        except (ShowError, OSError):
            pass

    def __enter__(self) -> "OscListener":
        return self

    def __exit__(self, *_exception) -> None:
        self.close()

    def describe(self) -> str:
        where = "every interface" if self.host in ("", "0.0.0.0") else self.host
        return f"listening on {where}:{self.port}"


@dataclass
class OscEvent:
    """One OSC line off the executable, parsed.

    `kind` is ``in`` (a message, with its address and arguments), ``fire``
    (a binding acted: its label and the 0..1 value) or ``out`` (a note from
    the output side, such as a name resolving).
    """
    kind: str
    address: str = ""
    arguments: List[object] = field(default_factory=list)
    label: str = ""
    value: float = 0.0
    text: str = ""


def parse_line(line: str) -> Optional[OscEvent]:
    """``OSC-IN /addr [0.5]`` / ``OSC-FIRE "label" 0.5`` / ``OSC-OUT ...``."""
    if line.startswith("OSC-IN "):
        payload = line[len("OSC-IN "):]
        if payload.startswith("dropped"):
            return OscEvent(kind="dropped", text=payload)
        address, _, rest = payload.partition(" ")
        try:
            arguments = json.loads(rest) if rest.strip() else []
        except ValueError:
            arguments = []
        return OscEvent(kind="in", address=address, arguments=list(arguments))
    if line.startswith("OSC-FIRE "):
        payload = line[len("OSC-FIRE "):]
        decoder = json.JSONDecoder()
        try:
            label, end = decoder.raw_decode(payload)
            value = float(payload[end:].strip() or 0.0)
        except ValueError:
            return None
        return OscEvent(kind="fire", label=str(label), value=value)
    if line.startswith("OSC-OUT "):
        return OscEvent(kind="out", text=line[len("OSC-OUT "):])
    return None


@dataclass
class Binding:
    """One incoming address, bound to one action. The file's shape; the
    executable runs it (eosc::Binding)."""
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
    max_interval: float = 0.2
    action: str = "param"
    params: Dict[str, object] = field(default_factory=dict)
    enabled: bool = True

    def matches(self, address: str) -> bool:
        """Glob, case-insensitively, minus anything `exclude` claims.

        A mirror of the executable's eosc::matchAddress, kept here so the
        panel can say which bindings take an address and a map can be checked
        without a show running. `*` crosses slashes on both sides.
        """
        folded = address.lower()
        if not fnmatch.fnmatch(folded, self.pattern.lower()):
            return False
        return not any(fnmatch.fnmatch(folded, veto.lower()) for veto in self.exclude)

    def to_dict(self) -> Dict[str, object]:
        return {
            "label": self.label,
            "trigger": {"pattern": self.pattern, "exclude": list(self.exclude),
                        "argument": self.argument},
            "mode": self.mode,
            "range": {"low": self.low, "high": self.high, "threshold": self.threshold},
            "limit": {"interval": self.min_interval, "change": self.min_change,
                      "keepalive": self.max_interval},
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
            max_interval=float(limit.get("keepalive", 0.2)),
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

    def find(self, label: str) -> Optional[Binding]:
        for binding in self.bindings:
            if binding.label == label:
                return binding
        return None


class Dispatcher:
    """Fired bindings in, actions out. The OSC twin of midi_map.Dispatcher.

    Fenced the same way and for the same reason: one binding aimed at a knob
    the running look does not have must not stop the binding after it, and the
    look under a cue button changes all night.
    """

    def __init__(self, bindings: BindingSet, context: ActionContext) -> None:
        self.bindings = bindings
        self.context = context
        self.last_said: Dict[str, str] = {}

    def hear(self, address: str) -> bool:
        """A message the executable reported: the one thing to learn from it
        is whether the app announced a scene."""
        return self.context.syn.hear(address)

    def handle_fire(self, label: str, value: float) -> List[str]:
        """Runs the action of the binding that fired. Returns what it said,
        once per change of what it says."""
        binding = self.bindings.find(label)
        if binding is None or not binding.enabled:
            return []
        spec = ACTIONS.get(binding.action)
        if spec is None:
            self.last_said[binding.label] = f"unknown action '{binding.action}'"
            return []
        try:
            line = spec.run(self.context, coerce_params(spec, binding.params), value)
        except Exception as error:
            self.last_said[binding.label] = str(error)
            return []
        if not line:
            self.last_said.pop(binding.label, None)
            return []
        if self.last_said.get(binding.label) != line:
            self.last_said[binding.label] = line
            return [line]
        return []

    def handle_line(self, line: str) -> List[str]:
        """One OSC line off the executable, whatever kind."""
        event = parse_line(line)
        if event is None:
            return []
        if event.kind == "in":
            self.hear(event.address)
            return []
        if event.kind == "fire":
            return self.handle_fire(event.label, event.value)
        return []
