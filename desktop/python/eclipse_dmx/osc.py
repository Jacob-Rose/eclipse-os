"""OSC out: the rig's colour, sent to something that renders pixels on a screen.

The first consumer is Synesthesia, whose scenes expose a `color` control that
can be driven over OSC — so the look running on the truss can tint the visuals
behind it, from one render, without either side knowing about the other.

Why this lives in python rather than in the executable
------------------------------------------------------
It does not have to stay here. But the executable already streams every
fixture's colour on stdout for the viewer (``--emit-frames``, see
``F rrggbb ...`` in main.cpp), and ShowController already parses that into a
Frame — so the whole feature is "read a colour that is already arriving, and
put it in a UDP packet". No C++, no build, no new transport in the show path.

The cost is one process hop of latency, which for a colour wash nobody can see,
and the fact that this only runs while python is driving the show. When both of
those stop being true, this becomes an ``OscOutput`` in the binary and the
packet builder below moves with it unchanged.

Which colour
------------
One fixture's, not an average. A rig is rarely one colour: the mean of a
palette sweep across a truss is grey, every time, and a "primary colour" that
is always grey is worse than no feature. So this samples a single fixture and
says so — pick the one that reads as the rig's colour from the front, or patch
a device with one fixture and place it where you want the probe (see
``devices/synesthesia.json``).

OSC, in thirty lines
--------------------
An OSC 1.0 message is an address string, a type tag string, then the arguments,
each block null-terminated and padded out to a multiple of four bytes, with
numbers big-endian. That is the entire format we need; there is no handshake,
no session and no reply, which is why this has no dependency.
"""

from __future__ import annotations

import socket
import struct
from typing import Optional, Sequence, Tuple

#: Where Synesthesia listens, unless its settings say otherwise.
#:
#: 6000 because that is what the app's own OSC panel comes up with —
#: ``OSC_INPUT_PORT`` in its ``preferences.json`` — and a default that matches
#: the other end's default is one fewer thing to get wrong on the night. OSC
#: itself has no standard port; this number is a convention between two
#: programs and nothing more. ``--address`` overrides it.
#:
#: Worth knowing that the *port* is the second setting, not the first: OSC
#: input also has to be switched on (``OSC_INPUT``), and it is a Pro feature.
#: Off, and every packet is discarded by the OS with no error either side —
#: which is exactly what a wrong port looks like too.
DEFAULT_ADDRESS = "127.0.0.1:6000"

#: The first colour control of whatever scene is running.
#:
#: Synesthesia offers two ways to name a control: this positional form, which
#: works on any scene, and `/controls/scene/<name>`, which is stable but only
#: for the one scene that has a control by that name. Positional is the right
#: default for a set that changes scenes; it is also the one that will point
#: somewhere unintended if a scene has its colour pickers in another order.
DEFAULT_CONTROL = "/controls/global/color/1"


def _pad(block: bytes) -> bytes:
    """Null-terminates and pads to the next four-byte boundary, per OSC 1.0."""
    block += b"\0"
    return block + b"\0" * (-len(block) % 4)


def encode(address: str, *values: float) -> bytes:
    """One OSC message: an address and zero or more float arguments.

    Floats only. Synesthesia's controls are all normalised 0..1 floats, and a
    builder that also did ints and strings would be three times the size for
    arguments nothing here sends.
    """
    tags = "," + "f" * len(values)
    body = b"".join(struct.pack(">f", float(value)) for value in values)
    return _pad(address.encode("ascii")) + _pad(tags.encode("ascii")) + body


def parse_endpoint(text: str, default_port: int = 6000) -> Tuple[str, int]:
    """``"host:port"`` -> ``("host", port)``. A bare host keeps the default."""
    host, separator, port = text.rpartition(":")
    if not separator:
        return text, default_port
    try:
        return (host or "127.0.0.1"), int(port)
    except ValueError:
        raise ValueError(f"'{text}' is not a host:port") from None


class SynesthesiaLink:
    """Sends one colour, repeatedly, to a colour control.

    Fire-and-forget by construction: UDP, no connection, no reply, and every
    error swallowed. That is deliberate rather than lazy — this is decoration
    hanging off the side of a show, and the visualiser being closed, restarted
    or on a laptop that went to sleep must never be able to interrupt the rig.
    The count of dropped sends is kept so a diagnostic can say so out loud.
    """

    def __init__(
        self,
        endpoint: str = DEFAULT_ADDRESS,
        control: str = DEFAULT_CONTROL,
        separate: bool = False,
        gamma: float = 0.0,
    ) -> None:
        self.host, self.port = parse_endpoint(endpoint)
        self.control = control.rstrip("/")

        # Two spellings of the same thing, because which one a build of
        # Synesthesia accepts is a question for the app and not for us: one
        # message carrying three floats, or three messages each carrying one
        # on the /r, /g and /b sub-addresses. Packed is fewer packets and is
        # what the docs describe; separate is the fallback that is hard to
        # argue with. Try packed, and if the control does not move, --separate.
        self.separate = separate

        # Undo the gamma the frame already carries.
        #
        # FixtureMap::render bakes master.gamma into every channel, because an
        # LED is linear in duty cycle and an eye is not. A shader painting a
        # screen is downstream of a display that corrects again, so passing the
        # corrected value on applies it twice and the visuals read washed out
        # and pale. 0 leaves the frame alone.
        self.gamma = gamma

        self.sent = 0
        self.dropped = 0
        self.last_error: Optional[str] = None

        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Connected UDP: lets the OS resolve and route once instead of per
        # packet, and turns a wrong hostname into an error here rather than
        # silently every frame for the length of a set.
        self._socket.connect((self.host, self.port))

    # -- sending ----------------------------------------------------------

    def send_color(self, rgb: Sequence[int]) -> None:
        """Sends one 0..255 RGB triple as normalised floats."""
        red, green, blue = (self._normalize(channel) for channel in rgb[:3])

        if self.separate:
            for suffix, value in (("r", red), ("g", green), ("b", blue)):
                self._send(encode(f"{self.control}/{suffix}", value))
        else:
            self._send(encode(self.control, red, green, blue))

    def send_raw(self, address: str, *values: float) -> None:
        """Any other control, for callers that want more than a colour."""
        self._send(encode(address, *values))

    def _normalize(self, channel: int) -> float:
        value = max(0.0, min(1.0, channel / 255.0))
        if self.gamma > 0.0:
            value = value ** (1.0 / self.gamma)
        return value

    def _send(self, packet: bytes) -> None:
        try:
            self._socket.send(packet)
            self.sent += 1
        except OSError as error:
            self.dropped += 1
            self.last_error = str(error)

    # Worth knowing what `dropped` does and does not catch: a bad host or an
    # unreachable network raises here, but nothing listening on the far port
    # usually does not. A closed UDP port is supposed to answer with ICMP
    # unreachable, which a connected socket would surface on a later send —
    # on Windows loopback it is simply not delivered, so a whole set aimed at
    # a visualiser that was never started reports zero errors. Which is why
    # this class does not offer an `is_connected`: there is no such thing.

    # -- lifecycle --------------------------------------------------------

    def close(self) -> None:
        self._socket.close()

    def __enter__(self) -> "SynesthesiaLink":
        return self

    def __exit__(self, *_exception) -> None:
        self.close()

    def describe(self) -> str:
        form = "r/g/b separately" if self.separate else "one message, 3 floats"
        gamma = f", ungamma {self.gamma:g}" if self.gamma > 0.0 else ""
        return f"{self.host}:{self.port} {self.control} ({form}{gamma})"
