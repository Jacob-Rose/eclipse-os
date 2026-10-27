"""OSC out: the rig's colour, sent to something that renders pixels on a screen.

The first consumer is Synesthesia, whose scenes expose a `color` control that
can be driven over OSC - so the look running on the truss can tint the visuals
behind it, from one render, without either side knowing about the other.

Where the bytes are made
------------------------
Not here. The executable owns the socket and the packet (the library's `eosc`
and `enet` modules, the same code a relic uses on its own WiFi), and streams
one fixture's colour from its frame loop. This module is the desk's handle on
that: `osc out`, `osc tap`, `osc send` and `osc status` over the control
protocol. Nothing in python encodes OSC any more, which is the point - one
implementation, tested once, on every platform the rig runs on.

Which colour
------------
One fixture's, not an average. A rig is rarely one colour: the mean of a
palette sweep across a truss is grey, every time, and a "primary colour" that
is always grey is worse than no feature. So the tap samples a single fixture
and says so - pick the one that reads as the rig's colour from the front, or
patch a device with one fixture and place it where you want the probe (see
``devices/synesthesia.json``).
"""

from __future__ import annotations

import json
import socket
from typing import Dict, Optional, Sequence, Tuple, Union

from .controller import ShowError

DEFAULT_ADDRESS = "127.0.0.1:6000"
DEFAULT_CONTROL = "/controls/global/color/1"

#: How often a hostname is looked up again while running, in seconds. A
#: machine that takes a new DHCP lease mid-set is not a reason to stop.
RESOLVE_INTERVAL = 15.0


def scene_address(name: str) -> str:
    """A scene name, as Synesthesia spells it in an OSC address.

    The app matches scenes on a folded form of the title: lowercase, with
    spaces, underscores and hyphens removed. "Neon Grid" is /scenes/neongrid.
    Folding here means mapping files can carry the human spelling.
    """
    folded = "".join(ch for ch in name.lower() if ch not in " _-")
    return f"/scenes/{folded}"


def control_address(name: str) -> str:
    """A scene control's name, as Synesthesia spells it in an OSC address.

    The same folding as a scene: `rig_color` is /controls/scene/rigcolor.
    By name rather than position because a name means the same control in
    every scene that has one, and nothing in a scene that does not.
    """
    folded = "".join(ch for ch in name.lower() if ch not in " _-")
    return f"/controls/scene/{folded}"


def parse_endpoint(text: str, default_port: int = 6000) -> Tuple[str, int]:
    """``"host:port"`` -> ``("host", port)``. A bare host keeps the default.

    The host may be a name rather than an address - ``mac-mini.local:6000`` -
    which the executable looks up, and keeps looking up.

    Three spellings of an address, and the brackets are not decoration: an IPv6
    literal is full of colons, so ``::1:6000`` cannot be split by the last one
    and ``[::1]:6000`` is the form that can. A bare literal with no brackets is
    taken whole, on the default port, because that is the only reading of it
    that is not a guess.
    """
    text = text.strip()
    if text.startswith("["):
        host, closed, rest = text[1:].partition("]")
        if not closed:
            raise ValueError(f"'{text}' has no closing ']'")
        if not rest:
            return host, default_port
        if not rest.startswith(":"):
            raise ValueError(f"'{text}' is not a host:port")
        try:
            return host, int(rest[1:])
        except ValueError:
            raise ValueError(f"'{text}' is not a host:port") from None
    if text.count(":") > 1:                 # a bare IPv6 literal; no port in it
        return text, default_port
    host, separator, port = text.rpartition(":")
    if not separator:
        return text, default_port
    try:
        return (host or "127.0.0.1"), int(port)
    except ValueError:
        raise ValueError(f"'{text}' is not a host:port") from None


def is_literal(host: str) -> bool:
    """Is this an address already, or a name that has to be looked up?"""
    for family in (socket.AF_INET, socket.AF_INET6):
        try:
            socket.inet_pton(family, host)
            return True
        except OSError:
            continue
    return False


def explain_failure(host: str, error: Union[BaseException, str]) -> str:
    """Why a name did not resolve, in the terms of the thing that is wrong.

    A `.local` name has two failures that read identically and are fixed in
    completely different places - the machine is not there, or this machine
    has no way to ask. Worth spending four lines to separate.
    """
    first = f"cannot resolve '{host}': {error}"
    if not host.lower().endswith(".local"):
        return first
    probe = f"avahi-resolve-host-name -4 {host}"
    return (
        f"{first}\n"
        "  '.local' is mDNS: the machine has to be on this network and awake,\n"
        "  and this one has to be able to ask. In that order:\n"
        f"    {probe}   # is it announcing?\n"
        f"    {'resolvectl mdns'.ljust(len(probe))}   # can we ask, on this link?\n"
        "  If neither answers: install avahi and nss-mdns, or turn on\n"
        "  MulticastDNS in systemd-resolved - or name the address instead."
    )


def parse_status(line: str) -> Dict[str, str]:
    """``OSC-OUT-STATUS open=yes endpoint=... error="..."`` -> a dict.

    Values are the executable's words; a quoted one is JSON and comes back
    decoded. Shared by the input side, whose status line has the same shape.
    """
    fields: Dict[str, str] = {}
    rest = line.split(None, 1)[1] if " " in line else ""
    while rest:
        rest = rest.lstrip()
        if not rest:
            break
        key, _, rest = rest.partition("=")
        if rest.startswith('"'):
            decoder = json.JSONDecoder()
            try:
                value, end = decoder.raw_decode(rest)
            except ValueError:
                value, end = rest, len(rest)
            rest = rest[end:]
            fields[key] = str(value)
        else:
            value, _, rest = rest.partition(" ")
            fields[key] = value
    return fields


def format_argument(value: Union[float, int, str, bool]) -> str:
    """One argument the way `osc send` reads it: a quoted string, a bare
    number, or true/false. The executable types them from the spelling."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(float(value))
    text = str(value).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{text}"'


class SynesthesiaLink:
    """The executable's OSC output, pointed at a visualiser.

    Fire-and-forget by construction: UDP, no connection, no reply, and every
    send error counted rather than raised. The visualiser being closed,
    restarted or on a laptop that went to sleep must never be able to
    interrupt the rig. The endpoint may name a machine rather than an address,
    in which case the executable looks the name up again every
    `resolve_every` seconds and follows it when it moves.

    `fixture` or `device` turns the colour tap on: that fixture's rendered
    colour goes to `control` every frame, from the frame loop, with no python
    in the path.
    """

    def __init__(
        self,
        show,
        endpoint: str = DEFAULT_ADDRESS,
        control: str = DEFAULT_CONTROL,
        separate: bool = False,
        gamma: float = 0.0,
        resolve_every: float = RESOLVE_INTERVAL,
        fixture: Optional[int] = None,
        device: Optional[str] = None,
    ) -> None:
        self.show = show
        self.endpoint = endpoint.strip()
        self.host, self.port = parse_endpoint(self.endpoint)
        self.control = control.rstrip("/")
        self.separate = separate
        self.gamma = gamma
        self.resolve_every = resolve_every
        self.is_name = not is_literal(self.host)
        self.address: Optional[str] = None
        self.sent = 0
        self.dropped = 0
        self.last_error: Optional[str] = None
        self.tapped: int = -1
        options = [
            f"control={self.control}",
            f"separate={'on' if separate else 'off'}",
            f"gamma={gamma:g}",
            f"resolve={resolve_every:g}",
        ]
        if device:
            options.append(f"device={device}")
        elif fixture is not None:
            options.append(f"fixture={int(fixture)}")
        try:
            reply = show.command(f"osc out {self.endpoint} " + " ".join(options))
        except ShowError as error:
            raise OSError(str(error)) from None
        self._take_status(reply)

    # -- status ----------------------------------------------------------------

    def _take_status(self, line: str) -> Dict[str, str]:
        fields = parse_status(line)
        address = fields.get("address", "-")
        self.address = None if address in ("", "-") else address
        self.sent = int(fields.get("sent", self.sent) or 0)
        self.dropped = int(fields.get("dropped", self.dropped) or 0)
        self.last_error = fields.get("error") or None
        try:
            self.tapped = int(fields.get("tap", self.tapped))
        except ValueError:
            pass
        if "control" in fields:
            self.control = fields["control"]
        return fields

    def refresh(self) -> Dict[str, str]:
        """Asks the executable where things stand: resolved address, counts,
        the last error. Cheap, but a round trip - once a second is plenty."""
        for line in self.show.query("osc status", "OSC-OUT-STATUS"):
            return self._take_status(line)
        return {}

    # -- sending ---------------------------------------------------------------

    def tap(self, fixture: Optional[int] = None, device: Optional[str] = None) -> None:
        """Streams a fixture's colour to the control every frame; neither
        named turns the tap off."""
        target = device if device else ("off" if fixture is None else str(int(fixture)))
        self.show.command(f"osc tap {target}")

    def send_color(self, rgb: Sequence[int]) -> None:
        """Sends one 0..255 RGB triple to the control, as normalised floats
        with the gamma undone - the same message the tap sends."""
        red, green, blue = (int(channel) for channel in rgb[:3])
        self.show.command(f"osc color {red} {green} {blue}", expect_reply=False)

    def send_raw(self, address: str, *values: Union[float, int, str]) -> None:
        """Any route, with any arguments."""
        words = " ".join(format_argument(value) for value in values)
        self.show.command(f"osc send {address} {words}".rstrip(), expect_reply=False)

    def send_scene(self, scene: str, preset: Optional[str] = None) -> None:
        """Launches a scene by name, optionally with one of its presets.

        The scene name is folded the way Synesthesia matches it (see
        scene_address); the preset name goes through verbatim, because on
        that side the app is case-sensitive.
        """
        if preset:
            self.send_raw(scene_address(scene), preset)
        else:
            self.send_raw(scene_address(scene))

    def send_preset(self, preset: str) -> None:
        """Loads a saved preset on whatever scene is running."""
        self.send_raw("/presets", preset)

    def send_favslot(self, slot: int) -> None:
        """Launches a favorites slot. The first slot is 1."""
        self.send_raw(f"/favslots/{int(slot)}")

    def send_media(self, name: str) -> None:
        """Selects a media file by name or full path."""
        self.send_raw("/media/name", name)

    def close(self) -> None:
        try:
            self.show.command("osc out off")
        except (ShowError, OSError):
            pass

    def __enter__(self) -> "SynesthesiaLink":
        return self

    def __exit__(self, *_exception) -> None:
        self.close()

    def describe(self) -> str:
        form = "r/g/b separately" if self.separate else "one message, 3 floats"
        gamma = f", ungamma {self.gamma:g}" if self.gamma > 0.0 else ""
        host = f"[{self.host}]" if ":" in self.host else self.host
        where = f"{host}:{self.port}"
        if self.is_name:
            where += f" -> {self.address}" if self.address else " (not resolved yet)"
        return f"{where} {self.control} ({form}{gamma})"
