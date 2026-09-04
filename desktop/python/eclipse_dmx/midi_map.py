"""MIDI mappings: a pad on a controller, bound to something the desk can do.

The executable already parses every channel message a MIDI port delivers -
`midi monitor on` streams them on stdout as `MIDI-IN ch=1 cc 40 127` lines,
and ShowController hands each one to `on_midi`. So there is no MIDI code
here at all: this module is what happens *after* a message arrives - which
pad means what, and what "what" is allowed to be.

The shape of a binding
----------------------
A `Mapping` is a trigger, a mode, and a list of actions:

    trigger   which messages this mapping listens for - kind (note / cc /
              program), channel (0 = any), and the note or controller number
    mode      press   fire once, when the pad goes down
              release fire when it comes back up
              value   fire on every message, carrying the 0..1 value - for
                      faders aimed at a control rather than pads aimed at a cue
    actions   any number of them, run in order. Each is a key into ACTIONS
              plus that action's own parameters.

The list is the shape a cue actually has. One pad is usually both halves of
the desk at once - Synesthesia moves to a scene *and* this rig moves to a
state - and to whoever hits the pad that is one thing, so it is one row. It
also gives the lamp side something to point at: a pad is lit as "live" when
the rig is in a state one of its actions sets, which only has an answer if
the pad owns both halves.

Order is honoured, and worth relying on: a `state` followed by a `param` is
a knob turned on the look that state just brought up.

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
from dataclasses import InitVar, dataclass, field
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

    #: Optional: is this action's effect in force *right now*?
    #:
    #: `check(context, params)` answers True, False, or None for "cannot
    #: know". None is the important one and the default: most actions are a
    #: verb with no readback - Synesthesia never says what scene it is on -
    #: and an action that guessed would be wrong the first time something
    #: else changed it. A no-opinion action neither confirms nor denies; see
    #: Mapping.is_live for how the votes are counted.
    #:
    #: This is what makes a lit surface a property of the registry rather
    #: than of the state machine. Nothing about lamps is written into the
    #: actions here - `check` says whether a thing is so, and the lamp code
    #: is the only thing that cares why.
    check: Optional[Callable[[ActionContext, Dict[str, object]], Optional[bool]]] = None


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


def _check_state(context, params):
    name = str(params.get("name", "")).strip()
    if not name or context.show is None:
        return None
    current = getattr(context.show, "current_state", None)
    if not current:
        # No state at all - a plain pattern is running. That is a real "no",
        # not an absence of information: whatever this pad sets, the rig is
        # certainly not in it.
        return False
    return current == name


def _run_pattern(context, params, value):
    name = str(params.get("name", "")).strip()
    if not name:
        return "pattern: no pattern named"
    if context.show is None:
        return "pattern: no show"
    context.show.set_pattern(name)
    return f"pattern {name}"


def _check_pattern(context, params):
    name = str(params.get("name", "")).strip()
    if not name or context.show is None:
        return None
    current = getattr(context.show, "current_pattern", None)
    if current is None:
        return None
    return current == name


def _run_param(context, params, value):
    """A knob on the running look, turned by a fader or by the audio engine.

    The one action that closes the loop with `osc_input`: Synesthesia's bass
    level arrives, is scaled here into the knob's own range, and goes down the
    protocol without waiting for the reply - see ShowController.set_param, and
    the note on rate in osc_input.

    `layer` aims it at a layer's look instead of the show's, because a rig
    where the UV par runs its own pattern is a rig where "intensity" is two
    different knobs.
    """
    name = str(params.get("name", "")).strip()
    if not name:
        return "param: no knob named"
    if context.show is None:
        return "param: no show"

    low = float(params.get("low", 0.0))
    high = float(params.get("high", 1.0))
    scaled = low + (high - low) * value

    layer = str(params.get("layer", "")).strip()
    target = context.show
    if layer:
        target = context.show.layers.get(layer)
        if target is None:
            # Named outright and not there: a typo, or a config whose layer was
            # renamed. Said rather than silently dropped, because a binding
            # aimed at nothing looks exactly like a binding that never fires.
            return f"param: no layer '{layer}'"

    # Checked here rather than left to the executable, because a streamed knob
    # does not wait for the reply - so an ERR for a knob this look has never
    # had would be drained unread, and a binding aimed at nothing would look
    # exactly like a binding that is working. `params` is what the look
    # announced on the last cue; empty means it has not announced yet, and
    # refusing then would refuse every binding for the first frames of a show.
    if target.params and target.get_param(name) is None:
        return f"param: the look has no '{name}'"

    target.set_param(name, scaled, wait=False)
    return None  # streamed; narrating every value would bury the header


def _run_master(context, params, value):
    """Rig-wide brightness. The bluntest audio binding there is, and the one
    worth having: the whole room breathing with the track."""
    if context.show is None:
        return "master: no show"
    low = float(params.get("low", 0.0))
    high = float(params.get("high", 1.0))
    context.show.set_master(low + (high - low) * value, wait=False)
    return None


def _run_bpm(context, params, value):
    """Tempo, from a value rather than a fixed line.

    Which makes Synesthesia's beat detector usable as this rig's clock: bind
    `syn_BPM` here, with the range it arrives in, and the desk takes its tempo
    from the audio engine instead of from Mixxx's note 50. Off by default in
    the shipped map, because two tempo sources disagreeing is worse than
    either alone - see osc_input and `midi status`.
    """
    if context.show is None:
        return "bpm: no show"
    low = float(params.get("low", 50.0))
    high = float(params.get("high", 220.0))
    context.show.command(f"bpm {low + (high - low) * value:.2f}", expect_reply=False)
    return None


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
    check=_check_state,
))

register_action(ActionSpec(
    key="pattern", label="rig: pattern",
    fields=[FieldSpec("name", "pattern",
                      hint="a pattern; for a state machine, which machine is loaded")],
    run=_run_pattern,
    check=_check_pattern,
))

register_action(ActionSpec(
    key="param", label="rig: look knob (value)",
    fields=[
        FieldSpec("name", "knob", hint="a knob on the running look, e.g. intensity"),
        FieldSpec("low", "low", kind="float", default=0.0, hint="value at 0"),
        FieldSpec("high", "high", kind="float", default=1.0, hint="value at 1"),
        FieldSpec("layer", "layer", hint="optional: a layer's look instead of the show's"),
    ],
    run=_run_param,
))

register_action(ActionSpec(
    key="master", label="rig: master brightness (value)",
    fields=[
        FieldSpec("low", "low", kind="float", default=0.0),
        FieldSpec("high", "high", kind="float", default=1.0),
    ],
    run=_run_master,
))

register_action(ActionSpec(
    key="bpm", label="rig: tempo (value)",
    fields=[
        FieldSpec("low", "low", kind="float", default=50.0, hint="bpm at 0"),
        FieldSpec("high", "high", kind="float", default=220.0, hint="bpm at 1"),
    ],
    run=_run_bpm,
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
class Action:
    """One thing a mapping does: a key into ACTIONS, and that action's own
    parameters.

    Split out from `Mapping` so one pad can do several things. A cue is
    usually both halves of the desk at once - the visualiser moves to a scene
    *and* this rig moves to a state - and that is one thing to whoever hits
    the pad, so it is one row here rather than two bound to the same note.

    The list is ordered and runs in order, which is worth relying on: a state
    followed by a knob is a knob turned on the look the state just brought up.
    """

    key: str = "syn_scene"
    params: Dict[str, object] = field(default_factory=dict)

    @property
    def spec(self) -> Optional[ActionSpec]:
        """The registry entry, or None for an action this build has never
        heard of - a map written by a newer desk, or a typo in a hand-edited
        file. Kept as data either way; see Dispatcher.handle."""
        return ACTIONS.get(self.key)

    def label(self) -> str:
        spec = self.spec
        return spec.label if spec is not None else self.key

    def to_dict(self) -> Dict[str, object]:
        return {"action": self.key, "params": dict(self.params)}

    @classmethod
    def from_dict(cls, data: Dict[str, object]) -> "Action":
        if not isinstance(data, dict):
            return cls()
        action = cls(key=str(data.get("action", "syn_scene")),
                     params=dict(data.get("params", {})))
        spec = action.spec
        if spec is not None:
            action.params = coerce_params(spec, action.params)
        return action


@dataclass
class Mapping:
    """One trigger, and the list of things it does."""

    label: str = "mapping"
    kind: str = "note"      # note / cc / program
    channel: int = 0        # 0 = any
    number: int = 60        # note, controller or program number
    mode: str = "press"     # press / release / value
    actions: List[Action] = field(default_factory=list)
    enabled: bool = True

    #: The colour this pad is lit, as a palette index the controller knows
    #: (0..127; see launchpad.Colour). One number rather than two, because the
    #: lit and live states are the same colour in different lighting types -
    #: static when the pad is merely bound, pulsing when the rig is in a state
    #: it sets. Ignored entirely by rigs with no lamps, which is most of them.
    colour: int = 41

    #: The one-action shorthand: `Mapping(action="state", params={...})`.
    #: Most mappings do one thing, and spelling `actions=[Action(...)]` at
    #: every such call site is noise. An InitVar rather than a field on
    #: purpose - it is folded into `actions` and then gone, so the actions
    #: live in exactly one place with no alias to drift out of step.
    action: InitVar[Optional[str]] = None
    params: InitVar[Optional[Dict[str, object]]] = None

    def __post_init__(self, action: Optional[str],
                      params: Optional[Dict[str, object]]) -> None:
        if action is not None or params is not None:
            self.actions = list(self.actions) + [
                Action(key=action or "syn_scene", params=dict(params or {}))]
        if not self.actions:
            # A mapping always does at least one thing, even if that thing has
            # not been chosen yet: the editor draws a form per action, and a
            # row with none would offer nowhere to start.
            self.actions = [Action()]

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
            "actions": [action.to_dict() for action in self.actions],
            "enabled": self.enabled,
            "colour": self.colour,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, object]) -> "Mapping":
        trigger = data.get("trigger", {})

        entries = data.get("actions")
        if entries is None:
            # A version 1 mapping: one action, spelled inline beside the
            # trigger. Read rather than migrated on disk - a map written
            # before actions were a list still opens, and is only rewritten
            # in the new shape once something saves it.
            entries = [{"action": data.get("action", "syn_scene"),
                        "params": data.get("params", {})}]
        elif not isinstance(entries, list):
            entries = []

        mapping = cls(
            label=str(data.get("label", "mapping")),
            kind=str(trigger.get("kind", "note")),
            channel=int(trigger.get("channel", 0)),
            number=int(trigger.get("number", 60)),
            mode=str(data.get("mode", "press")),
            actions=[Action.from_dict(entry) for entry in entries],
            enabled=bool(data.get("enabled", True)),
            colour=int(data.get("colour", 41)),
        )
        if mapping.kind not in TRIGGER_KINDS:
            mapping.kind = "note"
        if mapping.mode not in MODES:
            mapping.mode = "press"
        mapping.colour = max(0, min(127, mapping.colour))
        return mapping

    def describe_trigger(self) -> str:
        channel = "any" if self.channel == 0 else str(self.channel)
        return f"{self.kind} {self.number} ch {channel}"

    def describe_actions(self) -> str:
        """What the row does, short enough for the list. One action reads as
        itself; several are counted, because four labels in a 330px band
        would push the trigger off the end."""
        if len(self.actions) == 1:
            return self.actions[0].label()
        return f"{len(self.actions)} actions"

    def state_names(self) -> List[str]:
        """Every state this mapping would put the rig into.

        A list because a mapping may hold several actions and more than one of
        them may be a state - unusual, and not worth forbidding.
        """
        names = []
        for action in self.actions:
            if action.key == "state":
                name = str(action.params.get("name", "")).strip()
                if name:
                    names.append(name)
        return names

    def is_live(self, context: ActionContext) -> Optional[bool]:
        """Is what this pad does already so?

        Every action that can answer is asked, and they all have to agree:
        a pad that loads a machine *and* opens one of its looks is only the
        live one when both are true. That is what makes it a cue rather than
        two buttons - "jacket / campfire" is not lit on jacket alone.

        Actions that cannot know are not counted rather than counted as no.
        Most of the registry is verbs with no readback, and the common cue is
        a scene on the visualiser beside a state on the rig: if the scene -
        which can never answer - vetoed, that pad could never be live and the
        one thing this is for would not work.

        Returns None when *nothing* could answer, which is a different thing
        from False and is why this is not a bool. A row of pure Synesthesia
        actions has no answer available, and a surface that pulsed one anyway
        would be inventing it.
        """
        opinions: List[bool] = []
        for action in self.actions:
            spec = action.spec
            if spec is None or spec.check is None:
                continue
            try:
                verdict = spec.check(context, coerce_params(spec, action.params))
            except Exception:
                # Fenced like `run` is, and for the same reason: a check that
                # throws must cost this pad its lamp, not the whole repaint.
                verdict = None
            if verdict is not None:
                opinions.append(bool(verdict))

        if not opinions:
            return None
        return all(opinions)


# An InitVar with a default leaves that default sitting on the class, so
# `mapping.params` would answer None rather than raising - and None read as
# "no parameters" is exactly the silent wrong answer this shape exists to
# avoid. The generated __init__ has already captured both defaults, so taking
# them off the class costs nothing and makes the old spelling say so out loud.
del Mapping.action
del Mapping.params


class MappingSet:
    """The mappings, as a unit: what one controller layout does tonight.

    Saved and loaded whole - a file of these is a preset - and edited in
    place by the panel, which is why this is a class and not a bare list:
    the file format has one owner.
    """

    def __init__(self, mappings: Optional[List[Mapping]] = None) -> None:
        self.mappings: List[Mapping] = mappings or []

    #: Bumped when `actions` became a list. Version 1 files still load - see
    #: Mapping.from_dict - and are rewritten in this shape when next saved.
    VERSION = 2

    def to_dict(self) -> Dict[str, object]:
        return {"version": self.VERSION,
                "mappings": [m.to_dict() for m in self.mappings]}

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
            # Each action of the row is fenced separately, not just each row.
            # A pad that sets a scene and a state is one press to the person
            # who hit it, and half of it landing beats none of it: a dead
            # visualiser must not cost the rig its cue, which is the same
            # argument as the one across rows, one level down.
            for action in mapping.actions:
                spec = action.spec
                if spec is None:
                    said.append(f"{mapping.label}: unknown action '{action.key}'")
                    continue
                try:
                    line = spec.run(self.context,
                                    coerce_params(spec, action.params), value)
                except Exception as error:  # fenced on purpose; see class docstring
                    said.append(f"{mapping.label}: {error}")
                    continue
                if line:
                    said.append(line)
        return said
