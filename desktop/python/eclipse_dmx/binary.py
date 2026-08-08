"""Locating the eclipse-dmx executable.

The wrapper is useless without the binary, and the binary lands in a different
place depending on the generator, so this is worth doing properly once.
"""

from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import List, Optional, Union


class BinaryNotFoundError(FileNotFoundError):
    """Raised when eclipse-dmx cannot be located."""


def _executable_name() -> str:
    return "eclipse-dmx.exe" if os.name == "nt" else "eclipse-dmx"


def candidate_paths() -> List[Path]:
    """Everywhere we look, in order of preference."""
    name = _executable_name()

    # desktop/python/eclipse_dmx/binary.py -> desktop/
    desktop_root = Path(__file__).resolve().parent.parent.parent

    candidates = [
        desktop_root / "build" / name,                 # single-config generators
        desktop_root / "build" / "Release" / name,     # msvc, release
        desktop_root / "build" / "Debug" / name,       # msvc, debug
        desktop_root / "build" / "RelWithDebInfo" / name,
        desktop_root / "bin" / name,
    ]

    override = os.environ.get("ECLIPSE_DMX_BINARY")
    if override:
        candidates.insert(0, Path(override))

    return candidates


def find_executable(explicit: Optional[Union[str, Path]] = None) -> Path:
    """Returns the path to eclipse-dmx.

    Order: an explicit path, then $ECLIPSE_DMX_BINARY, then the usual build
    output directories, then PATH.
    """
    if explicit is not None:
        path = Path(explicit)
        if not path.exists():
            raise BinaryNotFoundError(f"no eclipse-dmx at '{path}'")
        return path.resolve()

    for candidate in candidate_paths():
        if candidate.exists():
            return candidate.resolve()

    on_path = shutil.which("eclipse-dmx")
    if on_path:
        return Path(on_path).resolve()

    searched = "\n  ".join(str(path) for path in candidate_paths())
    raise BinaryNotFoundError(
        "could not find the eclipse-dmx executable. Build it first:\n"
        "  cd desktop && ./build.sh        (linux/macos)\n"
        "  cd desktop; .\\build.ps1         (windows)\n"
        f"\nlooked in:\n  {searched}\nand on PATH"
    )
