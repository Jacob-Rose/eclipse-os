"""eclipse_dmx - python wrapper around the eclipse-dmx executable.

The executable owns timing and hardware. This package owns configuration:
building it, validating it before the rig sees it, writing it out, and then
driving the running process over its stdin protocol.

Typical use::

    from eclipse_dmx import Config, Fixture, ShowController

    config = Config()
    config.add_rgb_bank(count=4, first_channel=1)
    config.pattern.name = "palette_wave"

    with ShowController(config) as show:
        show.set_brightness(0.6)
        show.set_palette(["#ff0044", "#22ffcc"])
        show.wait()
"""

from .config import (
    BUILTIN_PALETTES,
    BUILTIN_PROFILES,
    PATTERN_NAMES,
    Config,
    ConfigError,
    DeviceConfig,
    Fixture,
    FixtureProfile,
    MasterConfig,
    PatternConfig,
)
from .controller import ShowController, ShowError
from .binary import find_executable, BinaryNotFoundError
from .ports import list_ports, SerialPortInfo

__version__ = "0.1.0"

__all__ = [
    "BUILTIN_PALETTES",
    "BUILTIN_PROFILES",
    "PATTERN_NAMES",
    "Config",
    "ConfigError",
    "DeviceConfig",
    "Fixture",
    "FixtureProfile",
    "MasterConfig",
    "PatternConfig",
    "ShowController",
    "ShowError",
    "find_executable",
    "BinaryNotFoundError",
    "list_ports",
    "SerialPortInfo",
]
