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

PATTERN_NAMES = ("solid", "palette_wave", "rainbow", "chase", "pulse", "off")

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
class PatternConfig:
    """The look, and its parameters."""

    name: str = "palette_wave"
    speed: float = 0.25
    width: float = 2.0
    brightness: float = 1.0
    palette: Union[str, List[Color]] = "p_bluemagic"
    color: Color = "#ff2200"

    def validate(self) -> None:
        if self.name not in PATTERN_NAMES:
            raise ConfigError(f"pattern.name '{self.name}' is not one of {PATTERN_NAMES}")
        if self.width <= 0:
            raise ConfigError(f"pattern.width {self.width} must be positive")
        if not 0.0 <= self.brightness <= 1.0:
            raise ConfigError(f"pattern.brightness {self.brightness} must be between 0 and 1")

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
        return {
            "name": self.name,
            "speed": self.speed,
            "width": self.width,
            "brightness": self.brightness,
            "palette": list(self.palette) if isinstance(self.palette, (list, tuple)) else self.palette,
            "color": self.color,
        }


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

    def validate(self) -> None:
        where = f"fixture '{self.name}'"

        if sorted(self.channels.lower()) != ["b", "g", "r"]:
            raise ConfigError(f"{where}: channels '{self.channels}' must be a permutation of r, g and b")

        if not 1 <= self.start_channel <= DMX_CHANNEL_COUNT:
            raise ConfigError(f"{where}: start_channel {self.start_channel} is outside 1..{DMX_CHANNEL_COUNT}")

        if self.start_channel + 2 > DMX_CHANNEL_COUNT:
            raise ConfigError(
                f"{where}: start_channel {self.start_channel} leaves no room for three colour channels"
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
        used = [self.start_channel + offset for offset in range(3)]
        if self.dimmer_channel:
            used.append(self.dimmer_channel)
        used.extend(int(channel) for channel in self.static_channels)
        return used

    def to_dict(self) -> Dict[str, Any]:
        out: Dict[str, Any] = {
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
    """A whole rig: device, trim, look and patch."""

    device: DeviceConfig = field(default_factory=DeviceConfig)
    master: MasterConfig = field(default_factory=MasterConfig)
    pattern: PatternConfig = field(default_factory=PatternConfig)
    fixtures: List[Fixture] = field(default_factory=list)

    # -- building ---------------------------------------------------------

    def add_fixture(self, fixture: Fixture) -> Fixture:
        self.fixtures.append(fixture)
        return fixture

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
        self.device.validate()
        self.master.validate()
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
        return {
            "device": self.device.to_dict(),
            "master": self.master.to_dict(),
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

        pattern = data.get("pattern", {})
        config.pattern = PatternConfig(
            name=pattern.get("name", config.pattern.name),
            speed=float(pattern.get("speed", config.pattern.speed)),
            width=float(pattern.get("width", config.pattern.width)),
            brightness=float(pattern.get("brightness", config.pattern.brightness)),
            palette=pattern.get("palette", config.pattern.palette),
            color=pattern.get("color", config.pattern.color),
        )

        for entry in data.get("fixtures", []):
            config.add_fixture(
                Fixture(
                    name=entry.get("name", f"fixture_{len(config.fixtures) + 1}"),
                    start_channel=int(entry.get("start_channel", 1)),
                    channels=entry.get("channels", "rgb"),
                    dimmer_channel=int(entry.get("dimmer_channel", 0)),
                    dimmer_value=int(entry.get("dimmer_value", 255)),
                    static_channels={
                        int(k): int(v) for k, v in (entry.get("static_channels") or {}).items()
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
