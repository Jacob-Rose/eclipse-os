"""Configuration model for eclipse-dmx.

This is the python side of the config file the executable reads. Keeping a
typed model here rather than hand-writing JSON means a rig can be described in
code, validated before anything is sent to hardware, and diffed between shows.

Validation is intentionally strict where the executable is forgiving: a channel
collision is a warning at runtime because a half-patched rig should still light
up, but if you are generating config from python you almost certainly want to
hear about it before you commit.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Union

DMX_CHANNEL_COUNT = 512

DEVICE_TYPES = ("enttec_pro", "enttec_open", "console")

PATTERN_NAMES = (
    # built-ins
    "solid",
    "palette_wave",
    "rainbow",
    "chase",
    "pulse",
    "identify",
    "off",
    # relic patterns, run unmodified through edmx::GeneratorPattern.
    # Must stay in step with ensureBuiltinsRegistered() in desktop/src/pattern.cpp.
    "obelisk_seasons",
    "obelisk_theater",
    "obelisk_mono",
    # a whole relic state machine, with its own looks and transitions
    "jacket",
    # the show: written for this rig, and beat-driven
    "mythos26",
)

#: The jacket's looks, in the order its state machine lists them. Must stay in
#: step with makeJacketStateMachine() in desktop/src/state_machine.cpp. The
#: executable is the authority — see patterns.list_states — this is here so a
#: config can be checked without a binary around.
JACKET_STATES = (
    "digital_void",
    "enchanted_forest",
    "warp_turbines",
    "rainbow_road",
    "breathe_with_me",
    "parrot",
    "system_overload",
    "cyber_toxin",
    "datamine",
    "blue_magic",
    "campfire",
    "hitstop",
)

#: mythos26's states, in the order its state machine lists them. Must stay in
#: step with makeMythos26StateMachine() in desktop/src/mythos26.cpp.
#:
#: slot_5 onwards are placeholders waiting for a look. Rename them here when
#: you rename them there.
MYTHOS26_STATES = (
    "beat_pulse",
    "vu_pulse",
    "tv_static_mono",
    "tv_static",
    "slot_5",
    "slot_6",
    "slot_7",
)

#: Which patterns are state machines, and what states each offers. Used to
#: check `pattern.state` before a binary is necessarily around.
STATE_MACHINE_STATES = {
    "jacket": JACKET_STATES,
    "mythos26": MYTHOS26_STATES,
}

ADDRESSING_MODES = ("one", "zero")

BUILTIN_PALETTES = (
    "p_retrosunset",
    "p_bluemagic",
    "p_darkpurple_neo",
    "p_disney100",
    "p_purplesky",
    "p_iceCream",
    "p_ritual",
    "p_parrot",
    "p_naturenight",
    "p_bootgradient",
    "p_nostalgicrain",
)

_HEX_COLOR = re.compile(r"^#?(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$")

Color = Union[str, Dict[str, float]]


class ConfigError(ValueError):
    """Raised when a config would not do what the caller meant."""


@dataclass
class FixtureProfile:
    """The channel layout of a fixture *model*, independent of where it sits.

    This is what a fixture's manual describes. Offsets are 1-based within the
    fixture, so a chart reading "CH1 dimmer, CH2 red" transcribes directly.

    ``park`` is the important part: cheap pars sit dark, or strobe, or run
    their own colour macro and ignore you entirely until their mode channels
    are pinned. Encoding that per model solves it once instead of per rig.
    """

    name: str
    footprint: int = 3
    red: int = 1
    green: int = 2
    blue: int = 3
    dimmer: int = 0          # 0 = the model has no master dimmer
    dimmer_value: int = 255
    park: Dict[int, int] = field(default_factory=dict)

    def validate(self) -> None:
        where = f"profile '{self.name}'"

        if not 1 <= self.footprint <= DMX_CHANNEL_COUNT:
            raise ConfigError(f"{where}: footprint {self.footprint} is out of range")

        claimed: Dict[int, str] = {}

        def claim(offset: int, what: str) -> None:
            if not 1 <= offset <= self.footprint:
                raise ConfigError(
                    f"{where}: {what} is at channel {offset}, "
                    f"outside the {self.footprint}-channel footprint"
                )
            if offset in claimed:
                raise ConfigError(f"{where}: channel {offset} is used by both {claimed[offset]} and {what}")
            claimed[offset] = what

        claim(self.red, "red")
        claim(self.green, "green")
        claim(self.blue, "blue")
        if self.dimmer:
            claim(self.dimmer, "dimmer")
        for offset, value in self.park.items():
            if not 0 <= int(value) <= 255:
                raise ConfigError(f"{where}: park value {value} at channel {offset} is outside 0..255")
            claim(int(offset), "a parked channel")

    def instantiate(self, name: str, address: int) -> "Fixture":
        """Places this model at `address`, the number set on its display."""
        return Fixture(
            name=name,
            start_channel=address,
            channels="rgb",
            _offsets=(self.red - 1, self.green - 1, self.blue - 1),
            dimmer_channel=(address + self.dimmer - 1) if self.dimmer else 0,
            dimmer_value=self.dimmer_value,
            static_channels={address + int(o) - 1: int(v) for o, v in self.park.items()},
        )

    def to_dict(self) -> Dict[str, Any]:
        out: Dict[str, Any] = {
            "footprint": self.footprint,
            "red": self.red,
            "green": self.green,
            "blue": self.blue,
        }
        if self.dimmer:
            out["dimmer"] = self.dimmer
            out["dimmer_value"] = self.dimmer_value
        if self.park:
            out["park"] = {str(k): int(v) for k, v in self.park.items()}
        return out


# Must stay in step with builtinProfiles() in desktop/src/fixture.cpp.
BUILTIN_PROFILES: Dict[str, FixtureProfile] = {
    # U'King Par 36, 7-channel mode:
    #   1 master dimmer  2 red  3 green  4 blue
    #   5 strobe  6 mode  7 colour selection
    "uking_par36": FixtureProfile(
        name="uking_par36",
        footprint=7,
        dimmer=1,
        red=2,
        green=3,
        blue=4,
        park={5: 0, 6: 0, 7: 0},
    ),
    "rgb3": FixtureProfile(name="rgb3", footprint=3, red=1, green=2, blue=3),
    "rgb4_dimmer": FixtureProfile(
        name="rgb4_dimmer", footprint=4, dimmer=1, red=2, green=3, blue=4
    ),
    "rgb7_par": FixtureProfile(
        name="rgb7_par", footprint=7, red=1, green=2, blue=3, dimmer=4,
        park={5: 0, 6: 0, 7: 0},
    ),
}


def _optional_float(value: Any) -> Optional[float]:
    """None stays None; anything else becomes a float."""
    return None if value is None else float(value)


def _check_color(value: Color, where: str) -> None:
    if isinstance(value, str):
        if not _HEX_COLOR.match(value):
            raise ConfigError(f"{where}: '{value}' is not a hex colour like '#ff0044'")
        return
    if isinstance(value, dict):
        unknown = set(value) - {"h", "s", "v"}
        if unknown:
            raise ConfigError(f"{where}: unexpected keys {sorted(unknown)}; expected h, s, v")
        return
    raise ConfigError(f"{where}: expected a hex string or an {{h, s, v}} dict, got {type(value).__name__}")


@dataclass
class DeviceConfig:
    """Which widget to talk to, and how fast."""

    type: str = "enttec_pro"
    port: str = "auto"

    # 115200 is what the PRO's firmware expects, and it is not negotiable:
    # there is a microcontroller behind the FTDI reading at a fixed rate, so a
    # "faster" link just hands it garbage and the fixtures flicker. What
    # actually buys headroom is sending fewer channels per frame.
    baud: int = 115200
    fps: float = 40.0
    console_channels: int = 12

    def validate(self) -> None:
        if self.type not in DEVICE_TYPES:
            raise ConfigError(f"device.type '{self.type}' is not one of {DEVICE_TYPES}")
        if not 1 <= self.fps <= 200:
            raise ConfigError(f"device.fps {self.fps} is out of range; DMX tops out around 44")
        if self.baud <= 0:
            raise ConfigError(f"device.baud {self.baud} must be positive")

    def to_dict(self) -> Dict[str, Any]:
        return {
            "type": self.type,
            "port": self.port,
            "baud": self.baud,
            "fps": self.fps,
            "console_channels": self.console_channels,
        }


@dataclass
class MasterConfig:
    """Rig-wide trim applied after the pattern and before the wire."""

    brightness: float = 1.0
    gamma: float = 2.2

    def validate(self) -> None:
        if not 0.0 <= self.brightness <= 1.0:
            raise ConfigError(f"master.brightness {self.brightness} must be between 0 and 1")
        if self.gamma < 0.0:
            raise ConfigError(f"master.gamma {self.gamma} must not be negative")

    def to_dict(self) -> Dict[str, Any]:
        return {"brightness": self.brightness, "gamma": self.gamma}


@dataclass
class MidiConfig:
    """Where the beat comes from.

    Optional in every sense: leave it alone and the rig keeps its own time at
    ``bpm``, which is what you want at a bench and a serviceable fallback on
    stage. Must stay in step with MidiConfig in desktop/include/edmx/config.h.

    The note numbers default to what Mixxx's MIDI-for-light mapping sends:
    note 50 on each beat, note 52 carrying the tempo as velocity + 50. Setting
    ``beat_note`` to -1 accepts *any* note as a beat, which is right for a
    source that sends nothing else and very wrong for Mixxx, whose VU meters
    are notes too.
    """

    enabled: bool = False
    port: str = "auto"
    #: Name fragments "auto" must never open, for the DJ controller on the same
    #: machine: it is a MIDI input, often the only one, and its pads all send
    #: notes. Naming ``port`` explicitly still overrides this.
    ignore: List[str] = field(default_factory=list)
    clock: bool = True
    notes: bool = True
    beat_note: int = 50
    bpm_note: int = 52
    #: The three loudness signals, all read and kept apart so a look can pick
    #: the one it wants. Mixxx's numbering: 64 instantaneous (every 40ms, peaks
    #: on every kick), 68 averaged over two seconds (the loudness of the
    #: track), 69 the first meter bar (quantised). -1 ignores one.
    vu_instant_note: int = 64
    vu_average_note: int = 68
    vu_meter_note: int = 69
    beat_channel: int = -1
    bpm: float = 128.0
    free_run: bool = True

    def ignores(self, name: str) -> bool:
        """Whether `name` matches the ignore list, the way the executable does."""
        lowered = name.lower()
        return any(fragment.lower() in lowered for fragment in self.ignore if fragment)

    def validate(self) -> None:
        if not 30.0 <= self.bpm <= 300.0:
            raise ConfigError(f"midi.bpm {self.bpm} is not a tempo; expected 30..300")

        for key in ("beat_note", "bpm_note",
                    "vu_instant_note", "vu_average_note", "vu_meter_note"):
            note = getattr(self, key)
            if note != -1 and not 0 <= note <= 127:
                raise ConfigError(f"midi.{key} {note} must be 0..127, or -1 to switch it off")

        if self.beat_channel != -1 and not 1 <= self.beat_channel <= 16:
            raise ConfigError(
                f"midi.beat_channel {self.beat_channel} must be 1..16, or -1 for any"
            )

        if self.beat_note != -1 and self.beat_note == self.bpm_note:
            raise ConfigError(
                "midi.beat_note and midi.bpm_note are the same note, so the tempo message "
                "would be taken as a beat"
            )

        for key in ("vu_instant_note", "vu_average_note", "vu_meter_note"):
            if self.beat_note != -1 and self.beat_note == getattr(self, key):
                raise ConfigError(
                    f"midi.{key} is the same note as midi.beat_note, so the meter would be "
                    "taken as a beat - dozens of times a second"
                )

        meters = [self.vu_instant_note, self.vu_average_note, self.vu_meter_note]
        live = [note for note in meters if note != -1]
        if len(set(live)) != len(live):
            raise ConfigError(
                f"two of midi.vu_*_note are the same note {live}; they carry different "
                "signals and cannot share one"
            )

        if self.enabled and not self.clock and not self.notes:
            raise ConfigError(
                "midi is enabled but both clock and notes are off, so nothing would set the tempo"
            )

        # An explicit name wins over the ignore list in the executable, so this
        # is not broken — but it is nobody's intent, and it reads as a config
        # that has been edited twice in opposite directions.
        if self.enabled and self.port != "auto" and self.ignores(self.port):
            raise ConfigError(
                f"midi.port '{self.port}' also matches midi.ignore {self.ignore}; "
                "the explicit port would win, so one of the two is a mistake"
            )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "enabled": self.enabled,
            "port": self.port,
            "ignore": list(self.ignore),
            "clock": self.clock,
            "notes": self.notes,
            "beat_note": self.beat_note,
            "bpm_note": self.bpm_note,
            "vu_instant_note": self.vu_instant_note,
            "vu_average_note": self.vu_average_note,
            "vu_meter_note": self.vu_meter_note,
            "beat_channel": self.beat_channel,
            "bpm": self.bpm,
            "free_run": self.free_run,
        }


@dataclass
class PatternConfig:
    """The look, and its parameters."""

    name: str = "palette_wave"
    speed: float = 0.25
    width: float = 2.0
    brightness: float = 1.0
    palette: Union[str, List[Color]] = "p_bluemagic"
    color: Color = "#ff2200"

    # Which look a state machine pattern opens on. Empty means the machine's
    # own first state, which for the jacket is digital_void - nearly black by
    # design, and a poor opening frame on a rig.
    state: str = ""

    # Overrides for the coordinate frame a pattern renders in. None means
    # "whatever the pattern itself asked for", which is almost always right: a
    # relic look knows the space it was tuned in. The obelisk's looks want an
    # 8 x 43 field from the origin; the jacket's want the monowire's line.
    coord_origin_x: Optional[float] = None
    coord_origin_y: Optional[float] = None
    coord_span_x: Optional[float] = None
    coord_span_y: Optional[float] = None

    def validate(self) -> None:
        if self.name not in PATTERN_NAMES:
            raise ConfigError(f"pattern.name '{self.name}' is not one of {PATTERN_NAMES}")
        if self.width <= 0:
            raise ConfigError(f"pattern.width {self.width} must be positive")
        if not 0.0 <= self.brightness <= 1.0:
            raise ConfigError(f"pattern.brightness {self.brightness} must be between 0 and 1")

        known_states = STATE_MACHINE_STATES.get(self.name)
        if self.state and known_states is not None and self.state not in known_states:
            raise ConfigError(
                f"pattern.state '{self.state}' is not one of {self.name}'s looks {known_states}"
            )

        # A zero span collapses the rig onto one coordinate, which makes any
        # spatial pattern render as flat colour. Easier to reject than to debug.
        for key in ("coord_span_x", "coord_span_y"):
            span = getattr(self, key)
            if span is not None and span <= 0:
                raise ConfigError(f"pattern.{key} must be positive, got {span}")

        if isinstance(self.palette, str):
            if self.palette not in BUILTIN_PALETTES:
                raise ConfigError(
                    f"pattern.palette '{self.palette}' is not a built-in; "
                    f"either use one of {BUILTIN_PALETTES} or pass an explicit list of colours"
                )
        elif isinstance(self.palette, (list, tuple)):
            if len(self.palette) < 2:
                raise ConfigError("pattern.palette needs at least two stops")
            for index, stop in enumerate(self.palette):
                _check_color(stop, f"pattern.palette[{index}]")
        else:
            raise ConfigError("pattern.palette must be a built-in name or a list of colours")

        _check_color(self.color, "pattern.color")

    def to_dict(self) -> Dict[str, Any]:
        out: Dict[str, Any] = {
            "name": self.name,
            "speed": self.speed,
            "width": self.width,
            "brightness": self.brightness,
            "palette": list(self.palette) if isinstance(self.palette, (list, tuple)) else self.palette,
            "color": self.color,
        }
        if self.state:
            out["state"] = self.state
        for key, value in (
            ("coord_origin_x", self.coord_origin_x),
            ("coord_origin_y", self.coord_origin_y),
            ("coord_span_x", self.coord_span_x),
            ("coord_span_y", self.coord_span_y),
        ):
            # Only written when set: an absent key means "whatever frame the
            # pattern itself asked for", and writing a number here would pin it.
            if value is not None:
                out[key] = value
        return out


@dataclass
class Fixture:
    """One patched fixture.

    ``start_channel`` is the address of the fixture's RED channel, which for a
    plain 3-channel par is the fixture's own address, and for a par with a
    dimmer in front of the colours is one past it. ``dimmer_channel`` and the
    keys of ``static_channels`` are absolute channel numbers, not offsets.
    """

    name: str
    start_channel: int
    channels: str = "rgb"
    dimmer_channel: int = 0
    dimmer_value: int = 255
    static_channels: Dict[int, int] = field(default_factory=dict)
    position: Optional[Sequence[float]] = None
    brightness: float = 1.0

    # Set when a profile placed this fixture: explicit (r, g, b) offsets from
    # start_channel, which lets a profile put the colour channels anywhere in
    # its footprint rather than requiring three in a row. None means derive
    # them from `channels`.
    _offsets: Optional[Sequence[int]] = None

    def offsets(self) -> Sequence[int]:
        """(r, g, b) offsets from start_channel."""
        if self._offsets is not None:
            return self._offsets
        order = self.channels.lower()
        return (order.index("r"), order.index("g"), order.index("b"))

    def validate(self) -> None:
        where = f"fixture '{self.name}'"

        if self._offsets is None and sorted(self.channels.lower()) != ["b", "g", "r"]:
            raise ConfigError(f"{where}: channels '{self.channels}' must be a permutation of r, g and b")

        if not 1 <= self.start_channel <= DMX_CHANNEL_COUNT:
            raise ConfigError(f"{where}: start_channel {self.start_channel} is outside 1..{DMX_CHANNEL_COUNT}")

        if self.start_channel + max(self.offsets()) > DMX_CHANNEL_COUNT:
            raise ConfigError(
                f"{where}: start_channel {self.start_channel} leaves no room for its colour channels"
            )

        if self.dimmer_channel and not 1 <= self.dimmer_channel <= DMX_CHANNEL_COUNT:
            raise ConfigError(f"{where}: dimmer_channel {self.dimmer_channel} is outside 1..{DMX_CHANNEL_COUNT}")

        if not 0 <= self.dimmer_value <= 255:
            raise ConfigError(f"{where}: dimmer_value {self.dimmer_value} is outside 0..255")

        for channel, value in self.static_channels.items():
            if not 1 <= int(channel) <= DMX_CHANNEL_COUNT:
                raise ConfigError(f"{where}: static channel {channel} is outside 1..{DMX_CHANNEL_COUNT}")
            if not 0 <= int(value) <= 255:
                raise ConfigError(f"{where}: static channel {channel} value {value} is outside 0..255")

        if not 0.0 <= self.brightness <= 1.0:
            raise ConfigError(f"{where}: brightness {self.brightness} must be between 0 and 1")

        if self.position is not None and len(self.position) < 1:
            raise ConfigError(f"{where}: position must have at least an x value")

    def used_channels(self) -> List[int]:
        """Every channel this fixture writes, for collision checking."""
        used = [self.start_channel + offset for offset in self.offsets()]
        if self.dimmer_channel:
            used.append(self.dimmer_channel)
        used.extend(int(channel) for channel in self.static_channels)
        return used

    def to_dict(self) -> Dict[str, Any]:
        # Profile-placed fixtures serialise to the fully explicit form, so the
        # written config says exactly which channel does what and never depends
        # on the executable resolving a profile the same way we did.
        if self._offsets is not None:
            r, g, b = self._offsets
            base = min(r, g, b)
            order = ["?"] * (max(r, g, b) - base + 1)
            order[r - base] = "r"
            order[g - base] = "g"
            order[b - base] = "b"

            if "?" not in order and len(order) == 3:
                # contiguous colour channels: expressible as a plain order string
                out: Dict[str, Any] = {
                    "name": self.name,
                    "start_channel": self.start_channel + base,
                    "channels": "".join(order),
                }
            else:
                raise ConfigError(
                    f"fixture '{self.name}': its profile puts the colour channels at "
                    f"offsets {self._offsets}, which the explicit config form cannot express. "
                    f"Write this rig with a \"profiles\" block instead."
                )
        else:
            out = {
                "name": self.name,
                "start_channel": self.start_channel,
                "channels": self.channels,
            }
        if self.dimmer_channel:
            out["dimmer_channel"] = self.dimmer_channel
            out["dimmer_value"] = self.dimmer_value
        if self.static_channels:
            # JSON object keys must be strings; the executable parses them back
            # to channel numbers.
            out["static_channels"] = {str(k): int(v) for k, v in self.static_channels.items()}
        if self.position is not None:
            out["position"] = list(self.position)
        if self.brightness != 1.0:
            out["brightness"] = self.brightness
        return out


@dataclass
class Config:
    """A whole rig: device, trim, look and patch.

    Channel numbers on `Fixture` are always one-based internally, whatever the
    file said. `addressing` records only how the file we read was *numbered*, so
    that anything printing channels back to a user can speak the same dialect
    their fixture displays do. See `display_channel`.
    """

    device: DeviceConfig = field(default_factory=DeviceConfig)
    master: MasterConfig = field(default_factory=MasterConfig)
    midi: MidiConfig = field(default_factory=MidiConfig)
    pattern: PatternConfig = field(default_factory=PatternConfig)
    fixtures: List[Fixture] = field(default_factory=list)

    addressing: str = "one"

    def display_channel(self, channel: int) -> int:
        """A one-based internal channel, back in the config's own numbering."""
        return channel - 1 if self.addressing == "zero" else channel

    # -- building ---------------------------------------------------------

    def add_fixture(self, fixture: Fixture) -> Fixture:
        self.fixtures.append(fixture)
        return fixture

    def add_bank(
        self,
        profile: Union[str, FixtureProfile],
        count: int,
        address: int = 1,
        name_prefix: Optional[str] = None,
        spacing: Optional[int] = None,
        spread_positions: bool = True,
    ) -> List[Fixture]:
        """Patch `count` fixtures of one model, back to back from `address`.

        The line you want for a rig of identical lights::

            config.add_bank("uking_par36", count=10, address=1)

        `address` is the fixture's own DMX address, the number set on its
        display. `spacing` defaults to the profile's footprint, which is what
        you want unless addresses were deliberately left with gaps.
        """
        if isinstance(profile, str):
            if profile not in BUILTIN_PROFILES:
                raise ConfigError(
                    f"unknown profile '{profile}' (built-ins: {sorted(BUILTIN_PROFILES)}); "
                    f"pass a FixtureProfile to define your own"
                )
            model = BUILTIN_PROFILES[profile]
        else:
            model = profile

        model.validate()

        if count < 1:
            raise ConfigError("add_bank needs a count of at least 1")

        step = model.footprint if spacing is None else spacing
        prefix = name_prefix if name_prefix is not None else model.name

        added: List[Fixture] = []
        for index in range(count):
            name = prefix if count == 1 else f"{prefix}_{index + 1}"
            fixture = model.instantiate(name, address + (index * step))

            if spread_positions and count > 1:
                fixture.position = [index / (count - 1), 0.0]

            added.append(self.add_fixture(fixture))

        return added

    def add_rgb_bank(
        self,
        count: int,
        first_channel: int = 1,
        channels_per_fixture: int = 3,
        order: str = "rgb",
        name_prefix: str = "fixture",
        spread_positions: bool = True,
    ) -> List[Fixture]:
        """Patch `count` identical RGB fixtures back to back.

        The common case: a row of pars at consecutive addresses. Positions are
        spread evenly across 0..1 so the spatial patterns sweep along the row
        in the order it is physically hung.
        """
        if count < 1:
            raise ConfigError("add_rgb_bank needs a count of at least 1")
        if channels_per_fixture < 3:
            raise ConfigError("add_rgb_bank needs at least 3 channels per fixture")

        added: List[Fixture] = []
        for index in range(count):
            position = None
            if spread_positions:
                x = 0.0 if count == 1 else index / (count - 1)
                position = [x, 0.0]

            fixture = Fixture(
                name=f"{name_prefix}_{index + 1}",
                start_channel=first_channel + (index * channels_per_fixture),
                channels=order,
                position=position,
            )
            added.append(self.add_fixture(fixture))

        return added

    # -- validating -------------------------------------------------------

    def validate(self, strict_overlap: bool = True) -> List[str]:
        """Raises ConfigError on anything unusable. Returns warnings.

        With `strict_overlap` a channel claimed by two fixtures is an error;
        turn it off if you are deliberately stacking fixtures on one address.
        """
        if self.addressing not in ADDRESSING_MODES:
            raise ConfigError(f"addressing '{self.addressing}' is not one of {ADDRESSING_MODES}")

        self.device.validate()
        self.master.validate()
        self.midi.validate()
        self.pattern.validate()

        if not self.fixtures:
            raise ConfigError("a config needs at least one fixture; there is nothing to light otherwise")

        warnings: List[str] = []
        claimed: Dict[int, str] = {}

        for fixture in self.fixtures:
            fixture.validate()

            for channel in fixture.used_channels():
                if channel > DMX_CHANNEL_COUNT:
                    warnings.append(
                        f"fixture '{fixture.name}' reaches channel {channel}, past the end of the universe"
                    )
                    continue

                owner = claimed.get(channel)
                if owner is not None and owner != fixture.name:
                    message = f"channel {channel} is claimed by both '{owner}' and '{fixture.name}'"
                    if strict_overlap:
                        raise ConfigError(message)
                    warnings.append(message)
                else:
                    claimed[channel] = fixture.name

        return warnings

    # -- serialising ------------------------------------------------------

    def to_dict(self) -> Dict[str, Any]:
        # Always written one-based, whatever the source file used, because the
        # fixtures below carry resolved one-based channels. Saying so explicitly
        # means a zero-based config that round-trips through here cannot come
        # back out and get biased a second time.
        return {
            "addressing": "one",
            "device": self.device.to_dict(),
            "master": self.master.to_dict(),
            "midi": self.midi.to_dict(),
            "pattern": self.pattern.to_dict(),
            "fixtures": [fixture.to_dict() for fixture in self.fixtures],
        }

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)

    def write(self, path: Union[str, Path]) -> Path:
        """Writes the config to `path`, validating first."""
        self.validate()
        target = Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(self.to_json(), encoding="utf-8")
        return target

    # -- loading ----------------------------------------------------------

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Config":
        config = cls()

        # Read first: every channel number below is interpreted through it.
        # Must stay in step with loadConfig() in desktop/src/config.cpp.
        stated = str(data.get("addressing", "one")).lower()
        if stated in ("zero", "zero-based", "0"):
            config.addressing = "zero"
            bias = 1  # a config address of 0 is DMX slot 1
        elif stated in ("one", "one-based", "1"):
            config.addressing = "one"
            bias = 0
        else:
            raise ConfigError(f"addressing must be \"zero\" or \"one\", got '{stated}'")

        device = data.get("device", {})
        config.device = DeviceConfig(
            type=device.get("type", config.device.type),
            port=device.get("port", config.device.port),
            baud=int(device.get("baud", config.device.baud)),
            fps=float(device.get("fps", config.device.fps)),
            console_channels=int(device.get("console_channels", config.device.console_channels)),
        )

        master = data.get("master", {})
        config.master = MasterConfig(
            brightness=float(master.get("brightness", config.master.brightness)),
            gamma=float(master.get("gamma", config.master.gamma)),
        )

        # Naming a port counts as asking for MIDI, the same way the executable
        # reads it - see loadConfig() in desktop/src/config.cpp.
        midi = data.get("midi", {})

        # A bare string is allowed for the common "just this one device" case,
        # matching the executable.
        stated_ignore = midi.get("ignore", [])
        if isinstance(stated_ignore, str):
            stated_ignore = [stated_ignore]

        config.midi = MidiConfig(
            enabled=bool(midi.get("enabled", "port" in midi)),
            port=midi.get("port", config.midi.port),
            ignore=[str(fragment) for fragment in stated_ignore if fragment],
            clock=bool(midi.get("clock", config.midi.clock)),
            notes=bool(midi.get("notes", config.midi.notes)),
            beat_note=int(midi.get("beat_note", config.midi.beat_note)),
            bpm_note=int(midi.get("bpm_note", config.midi.bpm_note)),
            vu_instant_note=int(midi.get("vu_instant_note", config.midi.vu_instant_note)),
            vu_average_note=int(midi.get("vu_average_note", config.midi.vu_average_note)),
            vu_meter_note=int(midi.get("vu_meter_note", config.midi.vu_meter_note)),
            beat_channel=int(midi.get("beat_channel", config.midi.beat_channel)),
            bpm=float(midi.get("bpm", config.midi.bpm)),
            free_run=bool(midi.get("free_run", config.midi.free_run)),
        )

        pattern = data.get("pattern", {})
        config.pattern = PatternConfig(
            name=pattern.get("name", config.pattern.name),
            speed=float(pattern.get("speed", config.pattern.speed)),
            width=float(pattern.get("width", config.pattern.width)),
            brightness=float(pattern.get("brightness", config.pattern.brightness)),
            palette=pattern.get("palette", config.pattern.palette),
            color=pattern.get("color", config.pattern.color),
            state=pattern.get("state", config.pattern.state),
            # absent stays absent, so the pattern's own frame survives
            coord_origin_x=_optional_float(pattern.get("coord_origin_x")),
            coord_origin_y=_optional_float(pattern.get("coord_origin_y")),
            coord_span_x=_optional_float(pattern.get("coord_span_x")),
            coord_span_y=_optional_float(pattern.get("coord_span_y")),
        )

        # A config's own profiles shadow the built-ins, matching the executable.
        profiles = dict(BUILTIN_PROFILES)
        for name, entry in (data.get("profiles") or {}).items():
            profiles[name] = FixtureProfile(
                name=name,
                footprint=int(entry.get("footprint", 3)),
                red=int(entry.get("red", 1)),
                green=int(entry.get("green", 2)),
                blue=int(entry.get("blue", 3)),
                dimmer=int(entry.get("dimmer", 0)),
                dimmer_value=int(entry.get("dimmer_value", 255)),
                park={int(k): int(v) for k, v in (entry.get("park") or {}).items()},
            )

        for entry in data.get("fixtures", []):
            profile_name = entry.get("profile")

            if profile_name:
                if profile_name not in profiles:
                    raise ConfigError(
                        f"unknown profile '{profile_name}' "
                        f"(built-ins: {sorted(BUILTIN_PROFILES)}; or define it under \"profiles\")"
                    )
                model = profiles[profile_name]

                address = entry.get("address", entry.get("start_channel"))
                if address is None:
                    raise ConfigError(
                        f"a fixture entry uses profile '{profile_name}' but has no \"address\""
                    )

                count = max(1, int(entry.get("count", 1)))
                step = int(entry.get("spacing", model.footprint))
                prefix = entry.get("name", profile_name)
                trim = float(entry.get("brightness", 1.0))
                stated_position = entry.get("position")

                for index in range(count):
                    name = prefix if count == 1 else f"{prefix}_{index + 1}"
                    fixture = model.instantiate(name, int(address) + bias + (index * step))
                    fixture.brightness = trim

                    if stated_position is not None:
                        fixture.position = list(stated_position)
                    elif count > 1:
                        fixture.position = [index / (count - 1), 0.0]

                    config.add_fixture(fixture)
                continue

            # An absent dimmer_channel means "this fixture has no dimmer" in
            # either numbering, so it stays 0 rather than being biased into
            # channel 1. An explicit 0 under zero-based addressing is a real
            # dimmer, in the first slot.
            stated_dimmer = entry.get("dimmer_channel")
            dimmer_channel = 0 if stated_dimmer is None else int(stated_dimmer) + bias

            config.add_fixture(
                Fixture(
                    name=entry.get("name", f"fixture_{len(config.fixtures) + 1}"),
                    start_channel=int(entry.get("start_channel", 1 - bias)) + bias,
                    channels=entry.get("channels", "rgb"),
                    dimmer_channel=dimmer_channel,
                    dimmer_value=int(entry.get("dimmer_value", 255)),
                    static_channels={
                        int(k) + bias: int(v)
                        for k, v in (entry.get("static_channels") or {}).items()
                    },
                    position=entry.get("position"),
                    brightness=float(entry.get("brightness", 1.0)),
                )
            )

        return config

    @classmethod
    def load(cls, path: Union[str, Path]) -> "Config":
        """Reads a config file.

        The executable tolerates ``//`` comments; python's json does not, so we
        strip them here to keep the two readers interchangeable.
        """
        text = Path(path).read_text(encoding="utf-8")
        return cls.from_dict(json.loads(_strip_line_comments(text)))


def _strip_line_comments(text: str) -> str:
    """Removes ``//`` comments that are not inside a string literal."""
    out = []
    in_string = False
    escaped = False
    index = 0

    while index < len(text):
        char = text[index]

        if in_string:
            out.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue

        if char == '"':
            in_string = True
            out.append(char)
            index += 1
            continue

        if char == "/" and index + 1 < len(text) and text[index + 1] == "/":
            while index < len(text) and text[index] != "\n":
                index += 1
            continue

        out.append(char)
        index += 1

    return "".join(out)
