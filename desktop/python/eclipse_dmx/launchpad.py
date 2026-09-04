"""The Launchpad X's lamps: which bytes light which pad, and nothing else.

Everything here is a pure function from intent to a list of bytes. Nothing in
this file opens a port, keeps a socket or knows what a mapping is - the bytes
go out through the executable's `midi send`, the same way every other piece of
hardware on this rig is reached. Which means the whole lamp protocol is
testable with no controller plugged in, and that matters: a controller is the
one part of a desk you cannot rely on having in front of you when the code is
being written.

Written from the Launchpad X Programmer's Reference Manual (Novation), and
worth stating because the interesting parts are not guessable:

  - **Live mode does not take host LED messages.** The manual says colours are
    accepted "in Lighting Custom Modes (and Programmer mode)" - so in the
    Session or Note layouts a note-on aimed at a pad does not light it. It is
    not silently ignored either; some of it lands and some does not, which
    reads exactly like a flaky cable and is not. `programmer_mode(True)` first,
    or nothing here works.

  - **Programmer mode changes what the pads send.** The grid becomes notes
    11..88 and the top row and right column become control changes. A binding
    learned in a custom mode was learned against different numbers, so it will
    need learning again - see the readme.

  - **One SysEx beats sixty note-ons.** `light()` builds the bulk LED message,
    which sets up to 81 pads at once. Sent as individual note-ons instead, a
    full repaint drops pads at random: the device's buffer fills and the tail
    is lost. A single message either arrives or does not.

  - **The LED indices are always Programmer-mode indices**, whatever layout is
    selected. So `pad(row, col)` is the one numbering worth knowing.

Colours are palette indices, 0..127, off the manual's table. There is no
attempt to name all of them here - `Colour` carries the handful the desk
actually uses, and any other number is equally valid.
"""

from __future__ import annotations

from typing import Iterable, List, Sequence, Tuple

#: Every message to the device starts with this. Manual: "All SysEx messages
#: begin with the following header regardless of direction."
HEADER: Tuple[int, ...] = (0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C)
END = 0xF7

#: Command bytes, after the header.
_CMD_LED = 0x03
_CMD_MODE = 0x0E

#: Lighting types, for a colour spec.
STATIC = 0
FLASHING = 1
PULSING = 2
RGB = 3

#: The manual's cap on one LED message. Beyond this the message is split.
MAX_SPECS = 81


class Colour:
    """The palette entries this desk uses, by name.

    Indices into the manual's colour table, which is fixed in the device - so
    these are not a theme, they are what the hardware calls these colours.
    """

    OFF = 0
    WHITE = 3
    RED = 5
    AMBER = 9
    YELLOW = 13
    GREEN = 21
    SPRING = 37
    CYAN = 41
    BLUE = 45
    PURPLE = 49
    MAGENTA = 53
    PINK = 57


# ---------------------------------------------------------------------------
# where the pads are
# ---------------------------------------------------------------------------

def pad(row: int, col: int) -> int:
    """The LED index of one square of the 8x8 grid, 1-based from bottom left.

    `pad(1, 1)` is the bottom-left pad (11) and `pad(8, 8)` the top-right (88),
    which is the manual's own numbering and the reason it is arithmetic rather
    than a table.
    """
    if not 1 <= row <= 8 or not 1 <= col <= 8:
        raise ValueError(f"pad {row},{col} is off the 8x8 grid")
    return row * 10 + col


#: The 8x8 grid, bottom row first - the order a repaint walks.
GRID: Tuple[int, ...] = tuple(pad(row, col) for row in range(1, 9) for col in range(1, 9))

#: The round buttons along the top, left to right. Control changes, not notes.
TOP_ROW: Tuple[int, ...] = tuple(range(91, 99))

#: The round buttons down the right, top to bottom.
RIGHT_COLUMN: Tuple[int, ...] = (89, 79, 69, 59, 49, 39, 29, 19)

#: The logo, which is an LED and not a button.
LOGO = 99

#: Everything that can be lit, which is what a full clear has to cover.
ALL_LEDS: Tuple[int, ...] = GRID + TOP_ROW + RIGHT_COLUMN + (LOGO,)


def is_lightable(index: int) -> bool:
    """True for an index the surface actually has a lamp at.

    Worth checking before building a message: a mapping bound to a pad in some
    other layout can carry a number this one has nowhere to put, and the device
    answers a bad index with silence rather than an error.
    """
    return index in _LIGHTABLE


_LIGHTABLE = frozenset(ALL_LEDS)


# ---------------------------------------------------------------------------
# the messages
# ---------------------------------------------------------------------------

def programmer_mode(enable: bool) -> List[int]:
    """Into Programmer mode, or back to Live.

    Nothing else in this module works until this has been sent - see the module
    docstring. Sending the Live-mode form on the way out is not optional
    politeness either: Programmer mode disables the device's own Setup button,
    so a desk that exits without it leaves the controller in a state its front
    panel cannot get out of.
    """
    return list(HEADER) + [_CMD_MODE, 1 if enable else 0, END]


def light(specs: Sequence[Tuple[int, int]]) -> List[List[int]]:
    """Static colours for a list of `(index, colour)` pairs.

    Returns a *list of messages*: one where it fits, more when there are more
    than 81 pads to set. The caller sends each in turn.
    """
    return _bulk([(STATIC, index, (colour,)) for index, colour in specs])


def pulse(specs: Sequence[Tuple[int, int]]) -> List[List[int]]:
    """As `light`, but the pads breathe.

    The device does the animation, which is the point of using it rather than
    repainting on a timer: a pulsing pad costs one message and then nothing,
    where blinking it from here would be two messages a second forever, down
    the same wire the beat arrives on.
    """
    return _bulk([(PULSING, index, (colour,)) for index, colour in specs])


def mixed(specs: Sequence[Tuple[int, int, int]]) -> List[List[int]]:
    """`(lighting_type, index, colour)` triples, so one message can hold both
    the static pads and the pulsing one - which is what a repaint actually is,
    and doing it in one message is what stops the live pad flickering as the
    static ones land around it."""
    return _bulk([(kind, index, (colour,)) for kind, index, colour in specs])


def clear(indices: Iterable[int] = ALL_LEDS) -> List[List[int]]:
    """Everything dark. The way out, and the way in - a repaint sets the pads
    it knows about, so anything left lit by whatever ran before would stay."""
    return light([(index, Colour.OFF) for index in indices])


def _bulk(entries: Sequence[Tuple[int, int, Sequence[int]]]) -> List[List[int]]:
    """The LED lighting SysEx, split at the manual's 81-entry limit.

    Each entry is a lighting type, an LED index and that type's data. Indices
    the surface has no lamp at are dropped rather than sent: they cost bytes
    on a wire that is also carrying the beat, and the device's answer to one
    is silence, so passing them on would only make a real fault harder to see.
    """
    usable = [entry for entry in entries if is_lightable(entry[1])]
    if not usable:
        return []

    messages: List[List[int]] = []
    for start in range(0, len(usable), MAX_SPECS):
        chunk = usable[start:start + MAX_SPECS]
        message = list(HEADER) + [_CMD_LED]
        for kind, index, data in chunk:
            message.append(kind)
            message.append(index)
            message.extend(max(0, min(127, int(value))) for value in data)
        message.append(END)
        messages.append(message)
    return messages


def as_hex(message: Sequence[int]) -> str:
    """`F0 00 20 29 ...` - the form `midi send` takes, and the form the manual
    prints, so a message can be read straight against the page."""
    return " ".join(f"{byte:02X}" for byte in message)


# ---------------------------------------------------------------------------
# what the surface should look like
# ---------------------------------------------------------------------------

class LampPainter:
    """Turns a mapping set plus the rig's current state into lamp messages.

    Two jobs, and it is worth naming them separately because only one of them
    is about MIDI:

      - **which pads mean something.** Every enabled mapping whose trigger
        number lands on a lamp gets lit in its own colour, so the surface
        shows what is bound without anyone having to remember.
      - **which one is live.** A mapping pulses when everything it does is
        already so - see `Mapping.is_live`. Nothing about that is written
        into this file: each action in the registry answers for itself, and
        the ones that cannot answer abstain. So "live" is whatever the
        actions mean by it, and a new kind of binding that can report its own
        state lights correctly here without this class being touched.

    In practice that comes out as the state machine, because `rig: state` and
    `rig: pattern` are the two that can answer - and it reads what the rig
    actually did rather than what was last pressed here, so a state changed
    from the desk, a shortcut or an OSC binding moves the lamp too. The
    Synesthesia half abstains: the visualiser never reports back what scene
    it is on, and a lamp that claimed to know would be wrong the first time a
    scene was changed from Synesthesia's own window.

    Nothing here sends. `frame()` returns messages; the caller owns the port.
    """

    def __init__(self, mappings, colour_of=None) -> None:
        self.mappings = mappings
        #: Overridable so a caller can theme the surface without touching the
        #: map - used by nothing yet, and the seam the desk would grow into.
        self._colour_of = colour_of or (lambda mapping: mapping.colour)
        #: What is currently on the surface, so a repaint that changes nothing
        #: sends nothing. The wire is shared with the beat.
        self._painted: dict = {}

    def wanted(self, context) -> dict:
        """`{led index: (lighting type, colour)}` for the surface as it should
        be. Pure - no device, no state kept - so a test can assert the picture
        rather than the bytes.

        `context` is the ActionContext the dispatcher fires against, because
        that is what an action's `check` is asked against: the surface and the
        bindings have to be looking at the same rig.
        """
        surface: dict = {}
        for mapping in self.mappings.mappings:
            if not mapping.enabled:
                continue
            index = mapping.number
            if not is_lightable(index):
                continue

            # None - nothing could answer - is lit but not pulsing, the same
            # as a plain no. The difference matters to the caller, not here.
            live = mapping.is_live(context) is True
            entry = (PULSING if live else STATIC, self._colour_of(mapping))

            # Two mappings can share a pad - that is how the map worked before
            # actions were a list, and old files still do it. Live wins, so a
            # pad whose other binding is a fader does not go static under it.
            if index in surface and surface[index][0] == PULSING:
                continue
            surface[index] = entry
        return surface

    def frame(self, context, force: bool = False) -> List[List[int]]:
        """The messages that move the surface from what it shows to what it
        should show. Empty when nothing changed.

        `force` repaints everything - for after a mode switch, when the device
        has forgotten what it was showing but this has not.
        """
        surface = self.wanted(context)

        if force:
            self._painted = {}

        changes = [(kind, index, colour)
                   for index, (kind, colour) in surface.items()
                   if self._painted.get(index) != (kind, colour)]

        # Pads that had a binding and no longer do, or whose mapping was
        # disabled. Left alone they would stay lit, claiming a pad does
        # something it does not.
        changes += [(STATIC, index, Colour.OFF)
                    for index in self._painted
                    if index not in surface]

        if not changes:
            return []

        self._painted = dict(surface)
        return mixed(changes)

    def forget(self) -> None:
        """Drop the record of what is on the surface, without painting.

        For when the device has been reset underneath us - unplugged, or put
        back into Live mode - so the next frame is a full repaint rather than
        a diff against a picture that is no longer there.
        """
        self._painted = {}
