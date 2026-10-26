"""Pattern discovery.

Delegated to the executable, for the same reason port discovery is: the
executable owns the registry, so it should be the thing that says what is in
it. `config.PATTERN_NAMES` is the static list used for validating a config
before a binary is necessarily around; this is the live one, and it picks up
relic patterns the moment they are registered in pattern.cpp.
"""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import List, Optional, Union

from .binary import find_executable


def list_patterns(
    executable: Optional[Union[str, Path]] = None, timeout: float = 10.0
) -> List[str]:
    """Every pattern the executable has registered, in its own order."""
    binary = find_executable(executable)

    result = subprocess.run(
        [str(binary), "--list-patterns"],
        capture_output=True,
        text=True,
        timeout=timeout,
    )

    return [
        line[len("PATTERN "):].strip()
        for line in result.stdout.splitlines()
        if line.startswith("PATTERN ")
    ]
