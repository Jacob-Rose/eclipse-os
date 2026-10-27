"""Look presets: a state's knobs, kept as a file and put back by name.

Tuning is live-only by design - `params dump` prints a line and walks away,
and nothing the sliders do lands in a config file behind you. This module is
the deliberate version of keeping: snapshot every param and curve of the
running look into a `.config` file beside the rig and device configs, and
replay it later onto whatever is running.

Replay matches by *name*, exactly like reset_look: each saved param is set
only if the running look has a knob of that name, and the rest are reported
rather than sent - so a preset saved on one state can be tried on a sibling
state that shares its vocabulary, and a preset that outlives a rename tells
you what it could not place instead of erroring out.

Files live in `<config dir>/looks/<state>/<name>.config`, one preset per
file - a directory per state so the dropdown for a cue is a directory
listing, and `.config` so they sit recognisably beside the environment and
device configs they belong with. Content is JSON, same as everything else.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

PRESET_SUFFIX = ".config"

#: (time, value, easing name or None) - controller.CurveKeyTuple's shape,
#: restated here so this module stands alone for tests
CurveKey = Tuple[float, float, Optional[str]]


def _safe(name: str) -> str:
    """A name as a filename: whatever the filesystem would fight over, folded
    to underscores. The pretty spelling lives inside the file."""
    cleaned = "".join(ch if (ch.isalnum() or ch in "-_ ") else "_" for ch in name.strip())
    return cleaned.replace(" ", "_") or "preset"


@dataclass
class LookPreset:
    """One kept look: where it came from, and every value it held."""

    name: str
    pattern: str = ""
    state: str = ""
    params: Dict[str, Union[float, str]] = field(default_factory=dict)
    curves: Dict[str, List[CurveKey]] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, object]:
        return {
            "version": 1,
            "name": self.name,
            "pattern": self.pattern,
            "state": self.state,
            "params": dict(self.params),
            "curves": {name: [list(key) for key in keys]
                       for name, keys in self.curves.items()},
        }

    @classmethod
    def from_dict(cls, data: Dict[str, object]) -> "LookPreset":
        curves: Dict[str, List[CurveKey]] = {}
        for name, keys in dict(data.get("curves", {})).items():
            curves[name] = [(float(key[0]), float(key[1]),
                             key[2] if len(key) > 2 and key[2] else None)
                            for key in keys]
        return cls(
            name=str(data.get("name", "preset")),
            pattern=str(data.get("pattern", "")),
            state=str(data.get("state", "")),
            params=dict(data.get("params", {})),
            curves=curves,
        )


def snapshot(show, name: str, pattern: str, state: str) -> LookPreset:
    """The running look, as a preset. Reads the controller's collected set -
    the values the executable last echoed, clamps and snaps included - so
    what is kept is what the look actually held, not what a slider sent."""
    return LookPreset(
        name=name,
        pattern=pattern,
        state=state,
        params={param.name: param.value for param in show.params},
        curves={curve: list(keys) for curve, keys in show.curves.items()},
    )


def apply_preset(show, preset: LookPreset) -> Tuple[int, List[str]]:
    """Puts a preset's values onto the running look, by name.

    Returns (how many landed, the names that had nowhere to go). Skipping is
    the contract, not a failure mode - see the module docstring.
    """
    applied = 0
    skipped: List[str] = []

    for name, value in preset.params.items():
        if show.get_param(name) is None:
            skipped.append(name)
            continue
        show.set_param(name, value)
        applied += 1

    for name, keys in preset.curves.items():
        if name not in show.curves:
            skipped.append(f"~{name}")
            continue
        show.set_curve(name, keys)
        applied += 1

    return applied, skipped


# -- the files --------------------------------------------------------------

def state_dir(directory: Union[str, Path], state: str) -> Path:
    return Path(directory) / _safe(state)


def preset_path(directory: Union[str, Path], state: str, name: str) -> Path:
    return state_dir(directory, state) / (_safe(name) + PRESET_SUFFIX)


def save_preset(directory: Union[str, Path], preset: LookPreset) -> Path:
    path = preset_path(directory, preset.state, preset.name)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(preset.to_dict(), indent=2) + "\n", encoding="utf-8")
    return path


def load_preset(path: Union[str, Path]) -> LookPreset:
    return LookPreset.from_dict(json.loads(Path(path).read_text(encoding="utf-8")))


def list_presets(directory: Union[str, Path], state: str) -> List[str]:
    """The presets kept for a state, by display name, sorted.

    Names are read out of the files rather than off the filenames, so a name
    the filesystem mangled still shows as it was typed.
    """
    folder = state_dir(directory, state)
    if not folder.is_dir():
        return []
    names: List[str] = []
    for path in sorted(folder.glob(f"*{PRESET_SUFFIX}")):
        try:
            names.append(load_preset(path).name)
        except (OSError, ValueError, KeyError):
            continue  # a broken file is not a reason to hide the rest
    return names
