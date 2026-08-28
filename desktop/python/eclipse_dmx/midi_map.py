"""MIDI mappings: a pad on a controller, bound to something the desk can do.

The executable already parses every channel message a MIDI port delivers -
`midi monitor on` streams them on stdout as `MIDI-IN ch=1 cc 40 127` lines,
and ShowController hands each one to `on_midi`. So there is no MIDI code
here at all: this module is what happens *after* a message arrives - which
pad means what, and what "what" is allowed to be.

The shape of a binding
----------------------
A `Mapping` is a trigger, a mode, and an action:

    trigger   which messages this mapping listens for - kind (note / cc /
              program), channel (0 = any), and the note or controller number
    mode      press   fire once, when the pad goes down
              release fire when it comes back up
              value   fire on every message, carrying the 0..1 value - for
                      faders aimed at a control rather than pads aimed at a cue
    action    a key into ACTIONS, plus that action's own parameters

Actions are a registry, not an enum, and that is the scalable part: an
`ActionSpec` names its parameters (so an editor can draw a form for any
action it has never heard of) and provides the code that runs it. Adding a
new kind of binding is one `register_action()` call - nothing in the
dispatcher, the file format, or the editor changes.

Actions run against an `ActionContext`: the running show, and an OSC link to
the visualiser (created on first use, so a mapping file full of Synesthesia
bindings costs nothing until one fires). The built-in set covers both sides
of the desk - Synesthesia scene / preset / favslot / control on one hand,
this rig's own state / pattern / protocol line on the other.

Files are one JSON object (see `MappingSet.to_dict`), saved and loaded whole:
a mapping file is a named preset for a controller layout.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Union


# ---------------------------------------------------------------------------
# what arrives
# ---------------------------------------------------------------------------

#: How the executable spells message kinds (describeMidiKind in main.cpp),
#: folded to the three a mapping can bind. Aftertouch, pressure and pitchbend
#: are deliberately not bindable yet: nothing on the desk wants them, and an
#: editor menu of seven kinds where three work is a menu that lies.
_KIND_FOLD = {
    "note_on": "note",
    "note_off": "note",
    "cc": "cc",
    "program": "program",
}

TRIGGER_KINDS = ("note", "cc", "program")
MODES = ("press", "release", "value")


def fold_kind(kind: str) -> Optional[str]:
    """The monitor's kind, as a trigger spells it - or None if not bindable."""
    return _KIND_FOLD.get(kind)


@dataclass(frozen=True)
class MidiEvent:
    """One channel message, as the monitor reported it."""

    kind: str      # note_on / note_off / cc / program / ...
    channel: int   # 1..16
    data1: int     # note or controller or program number
    data2: int     # velocity or value; 0 for program change

    @property
    def value(self) -> float:
        """The message's continuous value, 0..1. A program change has none
        and reads as full - a press, not a level."""
        if self.kind == "program":
            return 1.0
        return max(0, min(127, self.data2)) / 127.0

    def describe(self) -> str:
        return f"ch={self.channel} {self.kind} {self.data1} {self.data2}"


def parse_midi_line(payload: str) -> Optional[MidiEvent]:
    """`ch=1 cc 40 127` -> a MidiEvent, or None for anything else.

    "Anything else" includes the monitor's own `dropped N` note - a count,
    not a message - so a None here is normal traffic, not an error.
    """
    parts = payload.split()
    if len(parts) != 4 or not parts[0].startswith("ch="):
        return None
    try:
        return MidiEvent(
            kind=parts[1],
            channel=int(parts[0][len("ch="):]),
            data1=int(parts[2]),
            data2=int(parts[3]),
        )
    except ValueError:
        return None


# ---------------------------------------------------------------------------
# what an action is
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class FieldSpec:
    """One parameter an action takes, described well enough to draw a form.

    `kind` is "text", "int" or "float"; the editor validates on that, and
    `from_dict` re-coerces on load so a hand-edited file cannot smuggle a
    string into a slot number.
    """

    name: str
    label: str
    kind: str = "text"
    default: Union[str, int, float] = ""
    hint: str = ""


class ActionContext:
    """What actions run against: the show, and a link to the visualiser.

    The link is built lazily via `osc_factory` and kept - one socket for the
    life of the desk, not one per press - and `say` is one line to whatever
    status surface the host has.
    """

    def __init__(self, show=None,
                 osc_factory: Optional[Callable[[], object]] = None,
                 say: Optional[Callable[[str], None]] = None) -> None:
        self.show = show
        self._osc_factory = osc_factory
        self._osc = None
        self.say = say or (lambda message: None)

    def osc(self):
        if self._osc is None:
            if self._osc_factory is None:
                raise RuntimeError("no OSC link and no way to make one")
            self._osc = self._osc_factory()
        return self._osc


@dataclass(frozen=True)
class ActionSpec:
    """A kind of thing a mapping can do.

    `run(context, params, value)` performs it; `value` is the trigger's 0..1
    (always 1.0 in press mode). It returns a short line for the status
    surface, or None for silence - a fader streaming values ten times a
    second should not narrate every one.
    """

    key: str
    label: str
    fields: List[FieldSpec]
    run: Callable[[ActionContext, Dict[str, object], float], Optional[str]]


#: The registry, in the order an editor should offer them.
ACTIONS: Dict[str, ActionSpec] = {}


def register_action(spec: ActionSpec) -> ActionSpec:
    ACTIONS[spec.key] = spec
    return spec


# -- the built-in set -------------------------------------------------------

def _run_syn_scene(context, params, value):
    scene = str(params.get("scene", "")).strip()
    if not scene:
        return "syn scene: no scene named"
    preset = str(params.get("preset", "")).strip()
    context.osc().send_scene(scene, preset or None)
    return f"syn scene {scene}" + (f" + {preset}" if preset else "")


def _run_syn_preset(context, params, value):
    preset = str(params.get("preset", "")).strip()
    if not preset:
        return "syn preset: no preset named"
    context.osc().send_preset(preset)
    return f"syn preset {preset}"


def _run_syn_favslot(context, params, value):
    slot = int(params.get("slot", 1))
    context.osc().send_favslot(slot)
    return f"syn favslot {slot}"


def _run_syn_media(context, params, value):
    name = str(params.get("name", "")).strip()
    if not name:
        return "syn media: no media named"
    context.osc().send_media(name)
    return f"syn media {name}"


def _run_syn_control(context, params, value):
    address = str(params.get("address", "")).strip()
    if not address:
        return "syn control: no address"
    low = float(params.get("low", 0.0))
    high = float(params.get("high", 1.0))
    context.osc().send_raw(address, low + (high - low) * value)
    return None  # a fader narrating every packet would bury the header


def _run_state(context, params, value):
    name = str(params.get("name", "")).strip()
    if not name:
        return "state: no state named"
    if context.show is None:
        return "state: no show"
    context.show.set_state(name)
    return f"state {name}"


def _run_pattern(context, params, value):
    name = str(params.get("name", "")).strip()
    if not name:
        return "pattern: no pattern named"
    if context.show is None:
        return "pattern: no show"
    context.show.set_pattern(name)
    return f"pattern {name}"


def _run_command(context, params, value):
    line = str(params.get("line", "")).strip()
    if not line:
        return "command: empty line"
    if context.show is None:
        return "command: no show"
    context.show.command(line)
    return line


register_action(ActionSpec(
    key="syn_scene", label="syn: scene (+preset)",
    fields=[
        FieldSpec("scene", "scene", hint="Synesthesia scene name; spelled freely, normalised on send"),
        FieldSpec("preset", "preset", hint="optional, case-sensitive"),
    ],
    run=_run_syn_scene,
))

register_action(ActionSpec(
    key="syn_preset", label="syn: preset",
    fields=[FieldSpec("preset", "preset", hint="case-sensitive preset name")],
    run=_run_syn_preset,
))

register_action(ActionSpec(
    key="syn_favslot", label="syn: favslot",
    fields=[FieldSpec("slot", "slot", kind="int", default=1, hint="favorites slot, first is 1")],
    run=_run_syn_favslot,
))

register_action(ActionSpec(
    key="syn_media", label="syn: media",
    fields=[FieldSpec("name", "file", hint="media filename or full path")],
    run=_run_syn_media,
))

register_action(ActionSpec(
    key="syn_control", label="syn: control (value)",
    fields=[
        FieldSpec("address", "address", default="/controls/global/slider/1"),
        FieldSpec("low", "low", kind="float", default=0.0),
        FieldSpec("high", "high", kind="float", default=1.0),
    ],
    run=_run_syn_control,
))

register_action(ActionSpec(
    key="state", label="rig: state",
    fields=[FieldSpec("name", "state", hint="a state of the running machine")],
    run=_run_state,
))

register_action(ActionSpec(
    key="pattern", label="rig: pattern",
    fields=[FieldSpec("name", "pattern")],
    run=_run_pattern,
))

register_action(ActionSpec(
    key="command", label="rig: protocol line",
    fields=[FieldSpec("line", "line", hint="any protocol command, e.g. `bpm 128`")],
    run=_run_command,
))


def coerce_params(spec: ActionSpec, params: Dict[str, object]) -> Dict[str, object]:
    """Every field of `spec`, present and of its declared type.

    Applied on load and on edit, so `run` implementations can trust their
    parameters instead of each re-validating a hand-edited file.
    """
    clean: Dict[str, object] = {}
    for fld in spec.fields:
        raw = params.get(fld.name, fld.default)
        try:
            if fld.kind == "int":
                clean[fld.name] = int(raw)
            elif fld.kind == "float":
                clean[fld.name] = float(raw)
            else:
                clean[fld.name] = str(raw)
        except (TypeError, ValueError):
            clean[fld.name] = fld.default
    return clean


# ---------------------------------------------------------------------------
# the binding
# ---------------------------------------------------------------------------

@dataclass
class Mapping:
    """One pad, bound to one action."""

    label: str = "mapping"
    kind: str = "note"      # note / cc / program
    channel: int = 0        # 0 = any
    number: int = 60        # note, controller or program number
    mode: str = "press"     # press / release / value
    action: str = "syn_scene"
    params: Dict[str, object] = field(default_factory=dict)
    enabled: bool = True

    # -- matching ----------------------------------------------------------

    def matches(self, event: MidiEvent) -> bool:
        if _KIND_FOLD.get(event.kind) != self.kind:
            return False
        if self.channel != 0 and event.channel != self.channel:
            return False
        return event.data1 == self.number

    def fires_on(self, event: MidiEvent) -> Optional[float]:
        """The value to run with, or None when this event is the wrong edge.

        A pad speaks twice - down and up - as note_on/note_off or as a cc at
        127 then 0. press wants the first, release the second, and value
        wants everything it can hear.
        """
        if not self.matches(event):
            return None

        down = event.kind != "note_off" and event.data2 > 0
        if self.kind == "program":
            down = True  # a program change has no up

        if self.mode == "press":
            return 1.0 if down else None
        if self.mode == "release":
            return None if down else 1.0
        return event.value

    # -- the file ----------------------------------------------------------

    def to_dict(self) -> Dict[str, object]:
        return {
            "label": self.label,
            "trigger": {"kind": self.kind, "channel": self.channel, "number": self.number},
            "mode": self.mode,
            "action": self.action,
            "params": dict(self.params),
            "enabled": self.enabled,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, object]) -> "Mapping":
        trigger = data.get("trigger", {})
        mapping = cls(
            label=str(data.get("label", "mapping")),
            kind=str(trigger.get("kind", "note")),
            channel=int(trigger.get("channel", 0)),
            number=int(trigger.get("number", 60)),
            mode=str(data.get("mode", "press")),
            action=str(data.get("action", "syn_scene")),
            params=dict(data.get("params", {})),
            enabled=bool(data.get("enabled", True)),
        )
        if mapping.kind not in TRIGGER_KINDS:
            mapping.kind = "note"
        if mapping.mode not in MODES:
            mapping.mode = "press"
        spec = ACTIONS.get(mapping.action)
        if spec is not None:
            mapping.params = coerce_params(spec, mapping.params)
        return mapping

    def describe_trigger(self) -> str:
        channel = "any" if self.channel == 0 else str(self.channel)
        return f"{self.kind} {self.number} ch {channel}"


class MappingSet:
    """The mappings, as a unit: what one controller layout does tonight.

    Saved and loaded whole - a file of these is a preset - and edited in
    place by the panel, which is why this is a class and not a bare list:
    the file format has one owner.
    """

    def __init__(self, mappings: Optional[List[Mapping]] = None) -> None:
        self.mappings: List[Mapping] = mappings or []

    def to_dict(self) -> Dict[str, object]:
        return {"version": 1, "mappings": [m.to_dict() for m in self.mappings]}

    @classmethod
    def from_dict(cls, data: Dict[str, object]) -> "MappingSet":
        return cls([Mapping.from_dict(entry) for entry in data.get("mappings", [])])

    def save(self, path: Union[str, Path]) -> None:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_dict(), indent=2) + "\n", encoding="utf-8")

    @classmethod
    def load(cls, path: Union[str, Path]) -> "MappingSet":
        return cls.from_dict(json.loads(Path(path).read_text(encoding="utf-8")))


# ---------------------------------------------------------------------------
# firing
# ---------------------------------------------------------------------------

class Dispatcher:
    """Events in, actions out.

    One mapping failing must not stop the ones after it - a dead visualiser
    is exactly when the rig bindings still have to work - so each run is
    fenced and a refusal becomes a status line rather than an exception.
    """

    def __init__(self, mappings: MappingSet, context: ActionContext) -> None:
        self.mappings = mappings
        self.context = context

    def handle(self, event: MidiEvent) -> List[str]:
        """Runs every enabled mapping the event fires. Returns status lines."""
        said: List[str] = []
        for mapping in self.mappings.mappings:
            if not mapping.enabled:
                continue
            value = mapping.fires_on(event)
            if value is None:
                continue
            spec = ACTIONS.get(mapping.action)
            if spec is None:
                said.append(f"{mapping.label}: unknown action '{mapping.action}'")
                continue
            try:
                line = spec.run(self.context, coerce_params(spec, mapping.params), value)
            except Exception as error:  # fenced on purpose; see class docstring
                said.append(f"{mapping.label}: {error}")
                continue
            if line:
                said.append(line)
        return said
