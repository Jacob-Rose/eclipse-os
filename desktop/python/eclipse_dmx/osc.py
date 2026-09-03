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
import threading
from typing import Callable, Optional, Sequence, Tuple, Union

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

#: How often a *name* is looked up again while a set is running, in seconds.
#:
#: The endpoint may be a hostname - ``mac-mini.local:6000`` - and on a network
#: that hands out addresses by DHCP the machine behind that name can change
#: address mid-set. Resolving once at startup and connecting for good would aim
#: the rest of the set at an address nobody is at, and UDP would not say a word
#: about it (see the note on `dropped` below). So a name is looked up again on
#: this interval and the socket re-pointed if the answer moved.
#:
#: 15s is a compromise: a lookup is cheap and cached, a set is long, and the
#: window in which the visualiser is being sent packets at a stale address is
#: what this is buying down. It runs on its own thread and never in the send
#: path - a *failed* mDNS lookup takes seven seconds to give up, which is a
#: frame callback that would be blocked for seven seconds.
#:
#: An IP literal is never re-resolved; there is nothing to look up.
RESOLVE_INTERVAL = 15.0

#: How long the first look-up of a name is waited for before the show goes on
#: without it.
#:
#: A name that is up answers in milliseconds; a name that is not takes seven
#: seconds to fail, and those seven seconds are in front of the first frame of
#: a set. Nothing is lost by not waiting them out - the link keeps looking, and
#: picks the machine up when it appears - so this waits long enough to print
#: the address in the ordinary case and gives up quickly in the other.
FIRST_LOOKUP_WAIT = 2.0


def _pad(block: bytes) -> bytes:
    """Null-terminates and pads to the next four-byte boundary, per OSC 1.0."""
    block += b"\0"
    return block + b"\0" * (-len(block) % 4)


def encode(address: str, *values: Union[float, int, str]) -> bytes:
    """One OSC message: an address and zero or more arguments.

    Three types, tagged by what python type arrives: str -> `s`, int -> `i`,
    everything else through float() -> `f`. That is the whole of what
    Synesthesia's routes take - floats for controls, strings for scene and
    preset names, ints for slot and playlist positions - and OSC 1.0 has no
    other types this caller needs.
    """
    tags = ","
    body = b""
    for value in values:
        if isinstance(value, str):
            tags += "s"
            body += _pad(value.encode("utf-8"))
        elif isinstance(value, bool) or not isinstance(value, int):
            tags += "f"
            body += struct.pack(">f", float(value))
        else:
            tags += "i"
            body += struct.pack(">i", value)
    return _pad(address.encode("ascii")) + _pad(tags.encode("ascii")) + body


def decode(packet: bytes) -> List[Tuple[str, List[object]]]:
    """A UDP payload -> the messages in it, as ``(address, arguments)``.

    The other direction of `encode`, and needed for the same reason it was
    written by hand: this is an address, a type tag string and some big-endian
    numbers, and nothing that arrives here is worth a dependency.

    A bundle (`#bundle`) unpacks to the messages inside it, recursively, and
    its timetag is dropped — every sender aimed at this sends "immediately"
    and a desk that honoured a future timetag would be a desk that lags.

    Malformed input returns what could be read rather than raising. This is fed
    by a socket that anything on the network can write to, and a listener that
    dies on a stray packet is a listener that a stray packet can take a show
    down with.
    """
    if packet.startswith(b"#bundle\0"):
        messages: List[Tuple[str, List[object]]] = []
        offset = 16                                 # "#bundle\0" + timetag
        while offset + 4 <= len(packet):
            (size,) = struct.unpack_from(">i", packet, offset)
            offset += 4
            if size < 0 or offset + size > len(packet):
                break
            messages.extend(decode(packet[offset:offset + size]))
            offset += size
        return messages

    address, offset = _read_string(packet, 0)
    if address is None or not address.startswith("/"):
        return []

    tags, offset = _read_string(packet, offset)
    if tags is None or not tags.startswith(","):
        return [(address, [])]

    arguments: List[object] = []
    for tag in tags[1:]:
        if tag == "f":
            if offset + 4 > len(packet):
                break
            arguments.append(struct.unpack_from(">f", packet, offset)[0])
            offset += 4
        elif tag == "i":
            if offset + 4 > len(packet):
                break
            arguments.append(struct.unpack_from(">i", packet, offset)[0])
            offset += 4
        elif tag == "d":
            if offset + 8 > len(packet):
                break
            arguments.append(struct.unpack_from(">d", packet, offset)[0])
            offset += 8
        elif tag == "s":
            value, offset = _read_string(packet, offset)
            if value is None:
                break
            arguments.append(value)
        elif tag == "b":
            if offset + 4 > len(packet):
                break
            (size,) = struct.unpack_from(">i", packet, offset)
            offset += 4
            if size < 0 or offset + size > len(packet):
                break
            arguments.append(packet[offset:offset + size])
            offset += size + (-size % 4)
        elif tag in "TF":
            # A tag that carries its value in the tag itself, argument-less on
            # the wire. Worth having: a sender that spells a switch this way is
            # otherwise silently read as "no arguments".
            arguments.append(tag == "T")
        elif tag in "NI":
            arguments.append(None)
        else:
            break                                   # an unknown tag: the rest
                                                    # of the block is unreadable
    return [(address, arguments)]


def _read_string(packet: bytes, offset: int) -> Tuple[Optional[str], int]:
    """One null-terminated, four-byte-padded block, and where the next starts."""
    end = packet.find(b"\0", offset)
    if end < 0:
        return None, offset
    try:
        text = packet[offset:end].decode("utf-8")
    except UnicodeDecodeError:
        return None, offset

    # The block is the text, its null, and padding to the next four-byte
    # boundary - which is what _pad wrote on the way out.
    length = end + 1 - offset
    return text, offset + length + (-length % 4)


def scene_address(name: str) -> str:
    """A scene name, as Synesthesia spells it in an OSC address.

    The app matches scenes on a folded form of the title: lowercase, with
    spaces, underscores and hyphens removed. "Neon Grid" is /scenes/neongrid.
    Folding here means mapping files can carry the human spelling.
    """
    folded = "".join(ch for ch in name.lower() if ch not in " _-")
    return f"/scenes/{folded}"


def parse_endpoint(text: str, default_port: int = 6000) -> Tuple[str, int]:
    """``"host:port"`` -> ``("host", port)``. A bare host keeps the default.

    The host may be a name rather than an address - ``mac-mini.local:6000`` -
    which is the point of `resolve` below.

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


def resolve(host: str, port: int) -> Tuple[int, tuple]:
    """``("mac-mini.local", 6000)`` -> the family and sockaddr to connect to.

    Raises OSError when the name does not resolve, which for a ``.local`` name
    means either that the machine is not on the network right now or that this
    one cannot do mDNS at all (`explain_failure` tells them apart in words).

    **IPv4 is preferred when both are offered.** getaddrinfo's own order puts
    IPv6 first, and for a general client that is the right default - but the
    far end here is one app's UDP listener, and a listener bound to 0.0.0.0
    (which is what an OSC input usually is) cannot be reached over v6 at all.
    Sending into a socket nobody is listening on is this feature's signature
    failure and it is invisible from this side, so the more-likely-to-arrive
    family wins. A v6-only host still resolves - it is a preference, not a
    filter - and naming a literal forces the matter either way.

    The whole sockaddr is passed back rather than an address string because a
    link-local IPv6 answer carries a scope id, and it is not routable without.
    """
    answers = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)
    for family, _type, _proto, _canon, sockaddr in answers:
        if family == socket.AF_INET:
            return family, sockaddr
    family, _type, _proto, _canon, sockaddr = answers[0]
    return family, sockaddr


def explain_failure(host: str, error: BaseException) -> str:
    """Why a name did not resolve, in the terms of the thing that is wrong.

    A `.local` name has two failures that read identically from python and are
    fixed in completely different places - the machine is not there, or this
    machine has no way to ask. Worth spending four lines to separate.
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


class SynesthesiaLink:
    """Sends one colour, repeatedly, to a colour control.

    Fire-and-forget by construction: UDP, no connection, no reply, and every
    error swallowed. That is deliberate rather than lazy — this is decoration
    hanging off the side of a show, and the visualiser being closed, restarted
    or on a laptop that went to sleep must never be able to interrupt the rig.
    The count of dropped sends is kept so a diagnostic can say so out loud.

    The endpoint may name a machine rather than an address, in which case the
    name is looked up again on an interval and the socket re-pointed when the
    answer moves — see `resolve` and `RESOLVE_INTERVAL`. That is the same
    fire-and-forget contract applied to the address itself: a machine that took
    a new DHCP lease mid-set is not a reason to stop, and neither is one that
    has not turned up yet.
    """

    def __init__(
        self,
        endpoint: str = DEFAULT_ADDRESS,
        control: str = DEFAULT_CONTROL,
        separate: bool = False,
        gamma: float = 0.0,
        resolve_every: float = RESOLVE_INTERVAL,
        on_resolve: Optional[Callable[[str], None]] = None,
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

        #: Times the name moved to a different address under us, and times a
        #: look-up failed and we kept the address we had. Both are reported.
        self.rebinds = 0
        self.lookups_failed = 0

        #: What the name currently resolves to, or None if it never has.
        self.address: Optional[str] = None

        self._lock = threading.Lock()
        self._socket: Optional[socket.socket] = None
        self._sockaddr: Optional[tuple] = None
        self._on_resolve = on_resolve

        # A name is looked up now and again on a thread; an address is what it
        # is. Connected UDP either way: the OS resolves and routes once instead
        # of per packet, which is also what makes a re-point a visible event
        # here rather than something the kernel does silently per send.
        self.is_name = not is_literal(self.host)
        self._interval = resolve_every if self.is_name else 0.0
        self._stop = threading.Event()
        self._tried = threading.Event()
        self._watcher: Optional[threading.Thread] = None

        if not self.is_name:
            # An address that will not connect is a typo, and fatal as it has
            # always been.
            self._point_at(*resolve(self.host, self.port))
        elif self._interval <= 0.0:
            self._try_resolve()                 # once, blocking, and tolerated
        else:
            # A name is looked up on the thread that will keep looking it up,
            # and waited for only briefly. A machine that is asleep, off, or
            # slow to announce itself is a normal five minutes before doors -
            # not a reason to hold a show at the door or refuse to run it.
            self._watcher = threading.Thread(
                target=self._watch, name="osc-resolve", daemon=True)
            self._watcher.start()

            if not self._tried.wait(FIRST_LOOKUP_WAIT) and self.address is None:
                self.last_error = explain_failure(
                    self.host, f"no answer within {FIRST_LOOKUP_WAIT:g}s")

    # -- where it is pointing ---------------------------------------------

    def _point_at(self, family: int, sockaddr: tuple, announce: bool = False) -> None:
        """Connects a new socket to `sockaddr` and retires the old one.

        `announce` is off for the look-up in the constructor - the caller is
        about to print where this is sending anyway - and on for every one
        after it, which is a change nothing else would report.
        """
        opened = socket.socket(family, socket.SOCK_DGRAM)
        try:
            opened.connect(sockaddr)
        except OSError:
            opened.close()
            raise

        with self._lock:
            if self._stop.is_set():
                # close() ran while this look-up was in flight. A look-up can
                # take seven seconds and close() does not wait for one, so this
                # is reachable on the way out of every set that named a machine.
                opened.close()
                return
            retired, self._socket = self._socket, opened
            self._sockaddr = sockaddr
            self.address = sockaddr[0]

        # Outside the lock, and safe: a send holds the lock for the whole of
        # its send(), so nothing can still be holding the retired socket.
        if retired is not None:
            retired.close()
            self.rebinds += 1

        if announce and self._on_resolve is not None:
            self._on_resolve(self.address)

    def _try_resolve(self, announce: bool = False) -> bool:
        """One look-up. Never raises: a link with nowhere to send still runs."""
        try:
            family, sockaddr = resolve(self.host, self.port)
        except OSError as error:
            # Keep pointing where we were. A name that goes quiet for one
            # look-up is a wifi hiccup far more often than it is a machine that
            # moved, and tearing down a working link over it would turn a blip
            # into a dark visualiser for the rest of the set.
            self.lookups_failed += 1
            self.last_error = explain_failure(self.host, error)
            return False

        if sockaddr == self._sockaddr:
            return True

        try:
            self._point_at(family, sockaddr, announce=announce)
        except OSError as error:
            self.lookups_failed += 1
            self.last_error = str(error)
            return False
        return True

    def _watch(self) -> None:
        """Looks the name up, then keeps looking, on its own thread.

        Never in the send path. A failed mDNS look-up takes about seven seconds
        to time out, and that is seven seconds of frames not going anywhere if
        it happens where the colour is sent from.
        """
        while True:
            # Announce every look-up but the first: the first is what the
            # caller is about to print anyway.
            self._try_resolve(announce=self._tried.is_set())
            self._tried.set()
            if self._stop.wait(self._interval):
                return

    # -- sending ----------------------------------------------------------

    def send_color(self, rgb: Sequence[int]) -> None:
        """Sends one 0..255 RGB triple as normalised floats."""
        red, green, blue = (self._normalize(channel) for channel in rgb[:3])

        if self.separate:
            for suffix, value in (("r", red), ("g", green), ("b", blue)):
                self._send(encode(f"{self.control}/{suffix}", value))
        else:
            self._send(encode(self.control, red, green, blue))

    def send_raw(self, address: str, *values: Union[float, int, str]) -> None:
        """Any other route, for callers that want more than a colour."""
        self._send(encode(address, *values))

    # -- the app's other routes -------------------------------------------
    # Scene, preset, favslot, media: the desk telling the visualiser what to
    # *be*, where send_color tells it what colour it is. Same socket, same
    # fire-and-forget contract - a cue must land on the rig whether or not
    # anything is listening for the matching visual.

    def send_scene(self, scene: str, preset: Optional[str] = None) -> None:
        """Launches a scene by name, optionally with one of its presets.

        The scene name is folded the way Synesthesia matches it (see
        scene_address); the preset name goes through verbatim, because on
        that side the app is case-sensitive.
        """
        if preset:
            self._send(encode(scene_address(scene), preset))
        else:
            self._send(encode(scene_address(scene)))

    def send_preset(self, preset: str) -> None:
        """Loads a saved preset on whatever scene is running."""
        self._send(encode("/presets", preset))

    def send_favslot(self, slot: int) -> None:
        """Launches a favorites slot. The first slot is 1."""
        self._send(encode(f"/favslots/{int(slot)}"))

    def send_media(self, name: str) -> None:
        """Selects a media file by name or full path."""
        self._send(encode("/media/name", name))

    def _normalize(self, channel: int) -> float:
        value = max(0.0, min(1.0, channel / 255.0))
        if self.gamma > 0.0:
            value = value ** (1.0 / self.gamma)
        return value

    def _send(self, packet: bytes) -> None:
        # The lock is held across the send so a re-point cannot close the
        # socket out from under it; a UDP send at thirty a second is nowhere
        # near a contended lock.
        with self._lock:
            if self._socket is None:            # a name that has not resolved
                self.dropped += 1
                return
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
        self._stop.set()
        if self._watcher is not None:
            # It is a daemon thread, so this is politeness rather than a
            # requirement - but a look-up in flight holds no lock and the join
            # returns as soon as the wait() gives up, which is immediately.
            self._watcher.join(timeout=0.1)
            self._watcher = None
        with self._lock:
            if self._socket is not None:
                self._socket.close()
                self._socket = None

    def __enter__(self) -> "SynesthesiaLink":
        return self

    def __exit__(self, *_exception) -> None:
        self.close()

    def describe(self) -> str:
        form = "r/g/b separately" if self.separate else "one message, 3 floats"
        gamma = f", ungamma {self.gamma:g}" if self.gamma > 0.0 else ""

        # A name is printed with what it resolved to, because "sending to
        # mac-mini.local" is not a fact anyone can check and "-> 192.168.50.12"
        # is. An address prints as it always has.
        # Brackets back on an IPv6 literal, so what is printed is what could
        # be typed back in.
        host = f"[{self.host}]" if ":" in self.host else self.host
        where = f"{host}:{self.port}"
        if self.is_name:
            where += f" -> {self.address}" if self.address else " (not resolved yet)"

        return f"{where} {self.control} ({form}{gamma})"
