"""Port discovery - serial, for the DMX widget, and MIDI, for the beat.

Delegated to the executable rather than reimplemented with pyserial or
python-rtmidi: the executable is the thing that has to open the port, so it
should be the thing that decides what ports exist. Keeps the wrapper
dependency-free too.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Union

from .binary import find_executable


@dataclass
class SerialPortInfo:
    path: str
    description: str = ""

    def __str__(self) -> str:
        return f"{self.path}  {self.description}".rstrip()


def list_ports(executable: Optional[Union[str, Path]] = None, timeout: float = 10.0) -> List[SerialPortInfo]:
    """Serial ports the executable can see."""
    binary = find_executable(executable)

    result = subprocess.run(
        [str(binary), "--list-ports"],
        capture_output=True,
        text=True,
        timeout=timeout,
    )

    ports: List[SerialPortInfo] = []
    for line in result.stdout.splitlines():
        if not line.startswith("PORT "):
            continue
        payload = line[len("PORT "):]
        path, _, description = payload.partition("\t")
        ports.append(SerialPortInfo(path=path.strip(), description=description.strip()))

    return ports


@dataclass
class MidiPortInfo:
    index: int
    name: str = ""

    def __str__(self) -> str:
        return f"{self.index}  {self.name}".rstrip()


def list_midi_ports(
    executable: Optional[Union[str, Path]] = None, timeout: float = 10.0
) -> List[MidiPortInfo]:
    """MIDI inputs the executable can see, in the OS's own order.

    Either the index or a fragment of the name can be handed to
    ``ShowController.midi_open`` or ``--midi``.
    """
    binary = find_executable(executable)

    result = subprocess.run(
        [str(binary), "--list-midi"],
        capture_output=True,
        text=True,
        timeout=timeout,
    )

    ports: List[MidiPortInfo] = []
    for line in result.stdout.splitlines():
        if not line.startswith("MIDI "):
            continue
        payload = line[len("MIDI "):]
        index, _, name = payload.partition("\t")
        try:
            ports.append(MidiPortInfo(index=int(index.strip()), name=name.strip()))
        except ValueError:
            continue

    return ports
