"""Tests for the python wrapper and the viewer.

Plain unittest, so this needs nothing installed:

    cd desktop
    python -m unittest discover -s python/tests -v          # all of it
    python -m unittest discover -s python/tests -k Viewer   # one corner

Reach for `-k` while you are working. Most of this suite drives the real
executable and half of it opens a real window, so a full run is minutes; a
single class is seconds, and `-k` takes a class name, a test name or any
substring of either.

Anything that needs the executable or a display skips itself when there is not
one, so this stays runnable on a build machine and on a show laptop. What that
skip is, and how these tests wait, both live in `harness.py` - along with the
window-manager note, if a run keeps landing on top of what you are doing.
"""

from __future__ import annotations

import io
import json
import socket
import struct
import subprocess
import sys
import time
import unittest
from pathlib import Path

from harness import (ANSWER_FRAMES, PATIENCE, BinaryNotFoundError,  # noqa: E402
                     DESKTOP, GuiTest, ShowTest, executable_or_skip, run_show,
                     run_show_paced)
from eclipse_dmx.config import (  # noqa: E402
    AUDIO_CHANNELS,
    AUDIO_STATES,
    MYTHOS26_STATES,
    AudioConfig,
    Config,
    ConfigError,
    Fixture,
    MidiConfig,
)
from eclipse_dmx.controller import ShowController, ShowError, _parse_frame  # noqa: E402
from eclipse_dmx.curves import (  # noqa: E402
    EASING_NAMES,
    Curve,
    apply_easing,
    example_hit,
)
from eclipse_dmx import launchpad, look_presets, midi_map, osc, osc_input  # noqa: E402

RIG = DESKTOP / "config" / "uking_par36_x10.json"
SHOW = DESKTOP / "config" / "mythos26.json"
OBELISK = DESKTOP / "config" / "obelisk.json"
OBELISK_USB = DESKTOP / "config" / "obelisk_usb.json"
SCANNER = DESKTOP / "config" / "scanner.json"
STAGE = DESKTOP / "config" / "scanner_stage.json"

#: The bench rig for additive layers, and the only config that declares any.
#: The show used to carry three, patched permanently - see the note at the top
#: of that file for why a rig should not have an implicit global overlay.
LAYERS = DESKTOP / "config" / "audio_layers_test.json"

#: The sculpture's own numbers, from src/relics/obelisk/state_obelisk.h and the
#: eight GenerateAxisRow calls in obelisk.cpp. The config has to agree with
#: these or a look tuned on the desk is tuned on the wrong shape.
OBELISK_SIDE_LENGTH = 43
OBELISK_STRIPS = 8
OBELISK_PIXELS = OBELISK_SIDE_LENGTH * OBELISK_STRIPS


class FrameParsing(unittest.TestCase):
    """A malformed frame must never take the viewer down with it."""

    def test_well_formed(self):
        self.assertEqual(
            _parse_frame("F ff0000 00ff00 0000ff"),
            [(255, 0, 0), (0, 255, 0), (0, 0, 255)],
        )

    def test_empty(self):
        self.assertEqual(_parse_frame("F "), [])

    def test_truncated_swatch_rejected(self):
        self.assertIsNone(_parse_frame("F ff00"))

    def test_non_hex_rejected(self):
        self.assertIsNone(_parse_frame("F gggggg"))


class OscEncoding(unittest.TestCase):
    """The packet format, checked byte for byte.

    Worth pinning down here rather than against the app: OSC is unacknowledged,
    so a malformed packet is not rejected, it is ignored - and a control that
    does not move looks exactly the same as a visualiser that was never running.
    These tests are the only place the format gets to be wrong out loud.
    """
    def test_padding_and_layout(self):
        packet = osc.encode("/controls/global/color/1", 1.0, 0.5, 0.0)

        # Address (24 chars + null -> 28), tags ",fff" + null -> 8, 3 floats.
        self.assertEqual(len(packet), 48)
        self.assertEqual(packet[:24], b"/controls/global/color/1")
        self.assertEqual(packet[24:28], b"\0\0\0\0")
        self.assertEqual(packet[28:36], b",fff\0\0\0\0")

    def test_floats_are_big_endian(self):
        packet = osc.encode("/x", 1.0)
        self.assertEqual(packet[-4:], b"\x3f\x80\x00\x00")

    def test_every_block_is_four_byte_aligned(self):
        # The address lengths either side of a boundary are where an off-by-one
        # in the padding shows up, so walk a range rather than picking one.
        for length in range(1, 12):
            packet = osc.encode("/" + "a" * length, 0.25)
            self.assertEqual(len(packet) % 4, 0, f"address of {length + 1} chars")

    def test_no_arguments_still_valid(self):
        self.assertEqual(osc.encode("/bang"), b"/bang\0\0\0,\0\0\0")

    def test_endpoint_parsing(self):
        self.assertEqual(osc.parse_endpoint("127.0.0.1:8000"), ("127.0.0.1", 8000))
        self.assertEqual(osc.parse_endpoint("localhost"), ("localhost", 6000))
        with self.assertRaises(ValueError):
            osc.parse_endpoint("127.0.0.1:nope")

    def test_a_name_is_an_endpoint(self):
        # The whole point of naming a machine rather than an address: the
        # port is still the last colon, and a bare name still gets the default.
        self.assertEqual(osc.parse_endpoint("mac-mini.local:6000"),
                         ("mac-mini.local", 6000))
        self.assertEqual(osc.parse_endpoint("mac-mini.local"),
                         ("mac-mini.local", 6000))
        self.assertEqual(osc.parse_endpoint("  mac-mini.local:9000  "),
                         ("mac-mini.local", 9000))

    def test_ipv6_needs_its_brackets(self):
        # An IPv6 literal is full of colons, so the last one is not a port
        # separator. Bracketed, it can be; bare, the whole thing is the host.
        self.assertEqual(osc.parse_endpoint("[::1]:6000"), ("::1", 6000))
        self.assertEqual(osc.parse_endpoint("[fe80::1%wlan0]"), ("fe80::1%wlan0", 6000))
        self.assertEqual(osc.parse_endpoint("::1"), ("::1", 6000))
        with self.assertRaises(ValueError):
            osc.parse_endpoint("[::1:6000")

    def test_a_literal_is_told_from_a_name(self):
        # What decides whether anything is looked up again while running.
        self.assertTrue(osc.is_literal("127.0.0.1"))
        self.assertTrue(osc.is_literal("::1"))
        self.assertFalse(osc.is_literal("mac-mini.local"))
        self.assertFalse(osc.is_literal("localhost"))


class OscColour(unittest.TestCase):
    """What actually goes out for a given frame byte."""

    def setUp(self):
        self.sent = []

    def link(self, **kwargs):
        made = osc.SynesthesiaLink("127.0.0.1:1", **kwargs)
        # No socket involved. _send takes the address rather than a finished
        # packet - one place that knows *what* is going out, which is what
        # on_send and the osc panel need - so the encode moves in here to keep
        # these assertions about the bytes on the wire.
        made._send = lambda address, *values: self.sent.append(
            osc.encode(address, *values))
        return made

    def test_normalised_from_bytes(self):
        with self.link() as link:
            link.send_color([255, 128, 0])
        self.assertEqual(len(self.sent), 1)
        self.assertEqual(self.sent[0][:24], b"/controls/global/color/1")

    def test_separate_sends_three_messages(self):
        with self.link(separate=True) as link:
            link.send_color([255, 128, 0])
        self.assertEqual(len(self.sent), 3)
        for packet, suffix in zip(self.sent, (b"/r", b"/g", b"/b")):
            self.assertTrue(packet.startswith(b"/controls/global/color/1" + suffix))

    def test_gamma_is_undone_not_applied(self):
        # 128 is mid-grey after a 2.2 gamma, so undoing it must move *up*
        # towards linear. Getting the exponent the wrong way round would send
        # 0.22 instead of 0.73, which reads as "the visuals went dark".
        link = self.link(gamma=2.2)
        self.assertAlmostEqual(link._normalize(128), 0.7312, places=3)
        link.close()

    def test_out_of_range_is_clamped(self):
        link = self.link()
        self.assertEqual(link._normalize(-5), 0.0)
        self.assertEqual(link._normalize(300), 1.0)
        link.close()


class OscResolution(unittest.TestCase):
    """Naming a machine instead of an address, on a network that reassigns them.

    The failure this is all for is silent: UDP to an address nobody is at looks
    exactly like UDP to a visualiser that is running, so a link that resolved
    once at startup and never again would spend the rest of a set sending into
    a hole with a clean report at the end of it.

    `osc.resolve` is patched throughout rather than anything real being looked
    up - a test suite that needs a name on the network is a test suite that
    fails on the build machine.
    """
    def setUp(self):
        self.real_resolve = osc.resolve
        self.receivers = []

    def tearDown(self):
        osc.resolve = self.real_resolve
        for receiver in self.receivers:
            receiver.close()

    def receiver(self):
        """A bound UDP socket to be resolved *to*, and read back off."""
        import socket

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("127.0.0.1", 0))
        sock.settimeout(2.0)
        self.receivers.append(sock)
        return sock

    def answer(self, sock):
        import socket

        return (socket.AF_INET, sock.getsockname())

    def wait_for(self, predicate, seconds=3.0):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(0.01)
        return False

    def test_a_name_that_moves_is_followed(self):
        first, second = self.receiver(), self.receiver()
        answers = [self.answer(first)]
        osc.resolve = lambda host, port: answers[-1]

        moved = []
        link = osc.SynesthesiaLink("mac-mini.local:6000", resolve_every=0.05,
                                   on_resolve=moved.append)
        try:
            link.send_color([255, 0, 0])
            self.assertEqual(len(first.recv(512)), 48)

            # The machine comes back on a different address.
            answers.append(self.answer(second))
            self.assertTrue(self.wait_for(lambda: link.rebinds == 1),
                            "the link never followed the name")

            link.send_color([0, 255, 0])
            self.assertEqual(len(second.recv(512)), 48)
        finally:
            link.close()

        self.assertEqual(moved, [link.address])

    def test_a_failed_lookup_keeps_the_address_it_had(self):
        # A wifi hiccup is a far more common reason for one failed look-up than
        # a machine that moved, and going dark over it would turn a blip into a
        # visualiser that stays unlit for the rest of the set.
        first = self.receiver()

        def fails_after_the_first(host, port):
            osc.resolve = broken
            return self.answer(first)

        def broken(host, port):
            raise OSError("temporary failure in name resolution")

        osc.resolve = fails_after_the_first

        link = osc.SynesthesiaLink("mac-mini.local:6000", resolve_every=0.05)
        try:
            self.assertTrue(self.wait_for(lambda: link.lookups_failed >= 1))
            link.send_color([255, 0, 0])
            self.assertEqual(len(first.recv(512)), 48)
            self.assertEqual(link.rebinds, 0)
        finally:
            link.close()

    def test_a_name_that_never_resolves_does_not_stop_a_show(self):
        # The mac mini being asleep at soundcheck is not a reason to refuse to
        # run the rig. The link comes up with nowhere to send and keeps asking.
        osc.resolve = lambda host, port: (_ for _ in ()).throw(OSError("nope"))

        link = osc.SynesthesiaLink("mac-mini.local:6000", resolve_every=0.05)
        try:
            self.assertIsNone(link.address)
            link.send_color([255, 0, 0])          # must not raise
            self.assertEqual(link.sent, 0)
            self.assertEqual(link.dropped, 1)
            self.assertIn("mac-mini.local", link.describe())
        finally:
            link.close()

    def test_an_address_is_never_looked_up_again(self):
        # Nothing to look up, so no thread and no interval - and the reported
        # form of a literal stays exactly what it always was.
        link = osc.SynesthesiaLink("127.0.0.1:6000")
        try:
            self.assertFalse(link.is_name)
            self.assertIsNone(link._watcher)
            self.assertTrue(link.describe().startswith("127.0.0.1:6000 "))
        finally:
            link.close()

    def test_ipv4_wins_when_a_name_answers_with_both(self):
        # A visualiser's OSC input is usually bound to 0.0.0.0, which cannot be
        # reached over v6 at all - and being sent to the wrong family is the
        # invisible failure this whole feature is trying not to have.
        import socket

        both = [
            (socket.AF_INET6, socket.SOCK_DGRAM, 0, "", ("::1", 6000, 0, 0)),
            (socket.AF_INET, socket.SOCK_DGRAM, 0, "", ("127.0.0.1", 6000)),
        ]
        real = socket.getaddrinfo
        socket.getaddrinfo = lambda *a, **k: both
        try:
            self.assertEqual(self.real_resolve("mac-mini.local", 6000),
                             (socket.AF_INET, ("127.0.0.1", 6000)))
        finally:
            socket.getaddrinfo = real

    def test_a_v6_only_name_still_resolves(self):
        # Preference, not a filter.
        import socket

        only_v6 = [(socket.AF_INET6, socket.SOCK_DGRAM, 0, "", ("::1", 6000, 0, 0))]
        real = socket.getaddrinfo
        socket.getaddrinfo = lambda *a, **k: only_v6
        try:
            family, sockaddr = self.real_resolve("mac-mini.local", 6000)
            self.assertEqual(family, socket.AF_INET6)
            self.assertEqual(sockaddr[0], "::1")
        finally:
            socket.getaddrinfo = real

    def test_a_local_name_says_what_to_check(self):
        # The two failures behind a dead '.local' are fixed in completely
        # different places, and the message has to separate them.
        message = osc.explain_failure("mac-mini.local", OSError("no"))
        self.assertIn("mDNS", message)
        self.assertIn("avahi-resolve-host-name -4 mac-mini.local", message)
        self.assertEqual(osc.explain_failure("elsewhere.example", OSError("no")),
                         "cannot resolve 'elsewhere.example': no")


class OscDecoding(unittest.TestCase):
    """The other direction of the format, checked against our own encoder.

    Round trips rather than hand-built bytes for the ordinary cases - the
    encoder is already pinned byte for byte above, so agreeing with it is
    agreeing with OSC 1.0 - and hand-built bytes for the things the encoder
    never produces: bundles, and damage.
    """
    def test_round_trip(self):
        packet = osc.encode("/controls/global/color/1", 1.0, 0.5, 0.0)
        (address, arguments), = osc.decode(packet)
        self.assertEqual(address, "/controls/global/color/1")
        self.assertEqual([round(v, 3) for v in arguments], [1.0, 0.5, 0.0])

    def test_every_address_length_across_the_padding_boundary(self):
        for length in range(1, 12):
            address = "/" + "a" * length
            self.assertEqual(osc.decode(osc.encode(address, 0.25, "x", 7)),
                             [(address, [0.25, "x", 7])], f"address of {length + 1}")

    def test_no_arguments(self):
        self.assertEqual(osc.decode(osc.encode("/bang")), [("/bang", [])])

    def test_a_bundle_unpacks_to_its_messages(self):
        # Synesthesia is free to send one of these and the app's docs do not
        # say whether it does, so it has to be read either way.
        inner = [osc.encode("/a", 1.0), osc.encode("/b", 2.0)]
        bundle = (b"#bundle\0" + struct.pack(">q", 1)
                  + b"".join(struct.pack(">i", len(m)) + m for m in inner))
        self.assertEqual(osc.decode(bundle), [("/a", [1.0]), ("/b", [2.0])])

    def test_damage_returns_what_it_could_read(self):
        # Anything on the network can write to this port. A listener that
        # raises on a stray packet is a listener a stray packet can stop.
        self.assertEqual(osc.decode(b"garbage"), [])
        self.assertEqual(osc.decode(b""), [])
        self.assertEqual(osc.decode(osc.encode("/a", 1.0)[:-2]), [("/a", [])])
        self.assertEqual(osc.decode(b"#bundle\0" + b"\0" * 8 + b"\xff\xff\xff\xff"), [])

    def test_true_and_false_carry_no_argument_bytes(self):
        packet = osc.encode("/switch") [:-4] + b",T\0\0"
        self.assertEqual(osc.decode(packet), [("/switch", [True])])


class OscInputBindings(unittest.TestCase):
    """What a binding fires on, and - mostly - what it refuses to fire on."""

    def binding(self, **kwargs):
        return osc_input.Binding(**kwargs)

    def test_a_glob_survives_the_app_renaming_around_it(self):
        # The addresses these arrive on are not documented and changed once
        # already, which is the whole reason a binding is a pattern.
        bound = self.binding(pattern="*bass*level*")
        for address in ("/syn/BassLevel", "/audio/bass/level", "/SYN/BASSLEVEL"):
            self.assertTrue(bound.matches(address), address)
        self.assertFalse(bound.matches("/syn/MidLevel"))

    def test_exclude_keeps_the_bands_out_of_the_whole_spectrum(self):
        # Without this, one knob is driven by four sources at once.
        bound = self.binding(pattern="*level*", exclude=["*bass*", "*mid*", "*high*"])
        self.assertTrue(bound.matches("/syn/Level"))
        for band in ("/syn/BassLevel", "/syn/MidLevel", "/syn/MidHighLevel", "/syn/HighLevel"):
            self.assertFalse(bound.matches(band), band)

    def test_a_range_maps_onto_zero_to_one(self):
        # syn_BPM arrives at 50..220 and every action speaks 0..1.
        bound = self.binding(low=50.0, high=220.0)
        self.assertAlmostEqual(bound.scale(50.0), 0.0)
        self.assertAlmostEqual(bound.scale(135.0), 0.5)
        self.assertAlmostEqual(bound.scale(220.0), 1.0)
        self.assertAlmostEqual(bound.scale(500.0), 1.0)      # clamped, not wrapped
        self.assertAlmostEqual(bound.scale(0.0), 0.0)

    def test_a_level_that_holds_still_says_little(self):
        """Sixty identical values a second down the pipe the cues use - but
        not *nothing*, which is what this used to assert and what shipped.

        The far end fades a channel nobody has restated in 0.8s, as a
        dead-man's switch, so a binding that went permanently silent on a held
        value deleted it. See max_interval and TheBindingKeepsTheBusAlive.
        """
        bound = self.binding(min_interval=0.0, min_change=0.01)
        self.assertIsNotNone(bound.fires_on(0.5, 0.0))

        # Inside the keepalive window an unchanged value still says nothing,
        # which is the saving this exists for.
        self.assertIsNone(bound.fires_on(0.505, 0.05))

        # Past it, it restates itself rather than letting the value die.
        self.assertIsNotNone(bound.fires_on(0.505, 1.0))

        # And a real change is never delayed by any of it.
        self.assertIsNotNone(bound.fires_on(0.7, 1.01))

    def test_the_rate_limit_is_a_rate_limit(self):
        bound = self.binding(min_interval=0.1, min_change=0.0)
        self.assertIsNotNone(bound.fires_on(0.1, 10.0))
        self.assertIsNone(bound.fires_on(0.9, 10.05))
        self.assertIsNotNone(bound.fires_on(0.9, 10.2))

    def test_a_trigger_is_an_edge_and_is_never_dropped(self):
        # A beat arrives as a spike to 1.0 that stays up for a frame or two.
        # Firing on the level would fire every frame it is up; rate-limiting
        # it would drop beats, which is the one thing this mode cannot do.
        bound = self.binding(mode="trigger", threshold=0.5, min_interval=99.0)
        self.assertEqual(bound.fires_on(1.0, 0.0), 1.0)      # up: fires
        self.assertIsNone(bound.fires_on(1.0, 0.01))         # still up: silent
        self.assertIsNone(bound.fires_on(0.0, 0.02))         # down: silent
        self.assertEqual(bound.fires_on(1.0, 0.03), 1.0)     # up again, at once

    def test_a_message_with_no_number_is_not_a_zero(self):
        # A zero would be a value - a level of nothing, sent to a knob.
        self.assertIsNone(osc_input._number(["text"], 0))
        self.assertIsNone(osc_input._number([], 0))
        self.assertEqual(osc_input._number([0.25], 0), 0.25)
        self.assertEqual(osc_input._number([True], 0), 1.0)

    def test_a_file_round_trips(self):
        import tempfile

        original = osc_input.BindingSet([
            osc_input.Binding(label="bass", pattern="*bass*", exclude=["*mid*"],
                              mode="value", action="master",
                              params={"low": 0.5, "high": 1.0}),
        ])
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "map.json"
            original.save(path)
            loaded = osc_input.BindingSet.load(path)

        self.assertEqual(len(loaded.bindings), 1)
        self.assertEqual(loaded.bindings[0].pattern, "*bass*")
        self.assertEqual(loaded.bindings[0].exclude, ["*mid*"])
        self.assertEqual(loaded.bindings[0].params["high"], 1.0)


class OscInputActions(unittest.TestCase):
    """The actions an audio binding aims at, against a show that only records.

    These stream - `wait=False` - because a knob driven at frame rate cannot
    afford a round trip per value, and that is worth pinning: a regression
    to a blocking send would not fail anything, it would just quietly make the
    desk stutter under a bass line.
    """
    class FakeShow:
        def __init__(self):
            self.calls = []
            self.layers = {}
            self.params = []

        def get_param(self, name):
            return None

        def set_param(self, name, value, wait=True):
            self.calls.append(("param", name, value, wait))

        def set_master(self, value, wait=True):
            self.calls.append(("master", value, wait))

        def command(self, line, expect_reply=True):
            self.calls.append(("command", line, expect_reply))
            return "OK"

    def context(self):
        self.show = self.FakeShow()
        return midi_map.ActionContext(show=self.show)

    def run_action(self, key, params, value):
        spec = midi_map.ACTIONS[key]
        return spec.run(self.context(), midi_map.coerce_params(spec, params), value)

    def test_param_scales_into_the_knobs_own_range(self):
        self.run_action("param", {"name": "floor", "low": 0.0, "high": 0.35}, 1.0)
        self.assertEqual(self.show.calls, [("param", "floor", 0.35, False)])

    def test_param_streams_rather_than_waiting(self):
        self.run_action("param", {"name": "intensity"}, 0.5)
        self.assertFalse(self.show.calls[0][-1], "a streamed knob must not wait for a reply")

    def test_param_refuses_a_knob_the_look_does_not_have(self):
        # A streamed knob does not wait for the reply, so an ERR from the
        # executable would be drained unread and a binding aimed at nothing
        # would look exactly like one that is working.
        show = self.FakeShow()
        show.params = [object()]                       # the look has announced
        show.get_param = lambda name: None             # and has no such knob
        spec = midi_map.ACTIONS["param"]
        said = spec.run(midi_map.ActionContext(show=show),
                        midi_map.coerce_params(spec, {"name": "intensity"}), 0.5)
        self.assertIn("intensity", said)
        self.assertEqual(show.calls, [])

    def test_param_sends_before_a_look_has_announced_its_knobs(self):
        # Empty params means "not announced yet", not "has no knobs" - refusing
        # then would refuse every binding for the first frames of a show.
        show = self.FakeShow()
        show.params = []
        show.get_param = lambda name: None
        spec = midi_map.ACTIONS["param"]
        spec.run(midi_map.ActionContext(show=show),
                 midi_map.coerce_params(spec, {"name": "intensity"}), 0.5)
        self.assertEqual(len(show.calls), 1)

    def test_param_says_so_when_the_layer_is_not_there(self):
        # Aimed at nothing looks exactly like never firing, so it is said.
        said = self.run_action("param", {"name": "floor", "layer": "uv"}, 1.0)
        self.assertIn("uv", said)
        self.assertEqual(self.show.calls, [])

    def test_master_scales_and_streams(self):
        self.run_action("master", {"low": 0.55, "high": 1.0}, 0.0)
        self.assertEqual(self.show.calls, [("master", 0.55, False)])

    def test_bpm_maps_back_out_of_zero_to_one(self):
        self.run_action("bpm", {"low": 50.0, "high": 220.0}, 0.5)
        kind, line, expect_reply = self.show.calls[0]
        self.assertEqual(kind, "command")
        self.assertEqual(line, "bpm 135.00")
        self.assertFalse(expect_reply)


class OscInputListening(unittest.TestCase):
    """The socket, the decode and the dispatch, end to end over real UDP."""

    def test_a_packet_becomes_an_action(self):
        fired = []
        bindings = osc_input.BindingSet([
            osc_input.Binding(label="bass", pattern="*bass*", min_interval=0.0,
                              action="command", params={"line": "x"}),
        ])
        context = midi_map.ActionContext(show=None)
        dispatcher = osc_input.Dispatcher(bindings, context)

        listener = osc_input.OscListener(
            0, host="127.0.0.1",
            on_message=lambda address, arguments: fired.append(
                (address, arguments, dispatcher.handle(address, arguments))))
        try:
            port = listener._socket.getsockname()[1]
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sender.sendto(osc.encode("/syn/BassLevel", 0.75), ("127.0.0.1", port))

            deadline = time.monotonic() + 3.0
            while not fired and time.monotonic() < deadline:
                time.sleep(0.01)
            sender.close()
        finally:
            listener.close()

        self.assertTrue(fired, "nothing arrived on the listener")
        address, arguments, said = fired[0]
        self.assertEqual(address, "/syn/BassLevel")
        self.assertAlmostEqual(arguments[0], 0.75, places=5)
        # "command: no show" - it reached the action, which is the point here.
        self.assertTrue(said)

    def test_a_stray_packet_does_not_stop_the_port(self):
        seen = []
        listener = osc_input.OscListener(0, host="127.0.0.1",
                                         on_message=lambda a, v: seen.append(a))
        try:
            port = listener._socket.getsockname()[1]
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sender.sendto(b"not osc at all", ("127.0.0.1", port))
            sender.sendto(osc.encode("/after", 1.0), ("127.0.0.1", port))

            deadline = time.monotonic() + 3.0
            while not seen and time.monotonic() < deadline:
                time.sleep(0.01)
            sender.close()
        finally:
            listener.close()

        self.assertEqual(seen, ["/after"])
        self.assertEqual(listener.undecodable, 1)


class OscInputDispatch(unittest.TestCase):
    """One binding's refusal must not become a refusal thirty times a second."""

    def dispatcher(self, **binding):
        self.said = []
        bindings = osc_input.BindingSet([osc_input.Binding(
            label="one", pattern="*", min_interval=0.0, min_change=0.0, **binding)])
        return osc_input.Dispatcher(
            bindings, midi_map.ActionContext(show=None, say=self.said.append))

    def test_a_refusal_is_said_once(self):
        # "no show" here, but at a venue it is a knob the cued look lacks, and
        # the audio arrives at frame rate either way.
        dispatcher = self.dispatcher(action="master")
        lines = [dispatcher.handle("/syn/Level", [i / 20]) for i in range(20)]
        self.assertEqual(sum(len(one) for one in lines), 1)
        self.assertIn("master", dispatcher.last_said["one"])

    def test_it_speaks_again_when_it_has_something_new_to_say(self):
        dispatcher = self.dispatcher(action="param", params={"name": ""})
        first = dispatcher.handle("/syn/Level", [0.5])
        dispatcher.bindings.bindings[0].params = {"name": "x"}
        second = dispatcher.handle("/syn/Level", [0.6])
        self.assertTrue(first)
        self.assertTrue(second)
        self.assertNotEqual(first, second)

    def test_a_binding_that_says_nothing_is_forgotten(self):
        # An action that returns None succeeded silently - a streamed knob -
        # and the memo has to clear, or the next thing it does have to say
        # would be swallowed as a repeat.
        dispatcher = self.dispatcher(action="param", params={"name": ""})
        self.assertTrue(dispatcher.handle("/x", [0.5]))          # no knob named
        self.assertIn("one", dispatcher.last_said)

        dispatcher.context.show = OscInputActions.FakeShow()
        dispatcher.bindings.bindings[0].params = {"name": "floor"}
        self.assertEqual(dispatcher.handle("/x", [0.6]), [])     # streamed, silent
        self.assertNotIn("one", dispatcher.last_said)


class TheShippedOscMap(unittest.TestCase):
    """The map that ships beside the show, against the code that runs it.

    A binding naming an action that does not exist, or a knob no look has, is
    a binding that does nothing and says nothing until someone plays a track.
    """
    MAP = DESKTOP / "config" / "oscmaps" / "synesthesia.json"

    def setUp(self):
        if not self.MAP.exists():
            self.skipTest("no shipped osc map")
        self.bindings = osc_input.BindingSet.load(self.MAP).bindings

    def test_it_loads_and_has_bindings(self):
        self.assertTrue(self.bindings)

    def test_every_action_exists(self):
        for binding in self.bindings:
            self.assertIn(binding.action, midi_map.ACTIONS, binding.label)

    def test_every_mode_is_a_mode(self):
        for binding in self.bindings:
            self.assertIn(binding.mode, osc_input.MODES, binding.label)

    def test_the_enabled_ones_name_knobs_a_look_here_has(self):
        # The knobs the beat flash and the static pair announce; see
        # readme.md, "the looks".
        known = {"attack", "decay", "intensity", "floor", "hold", "rate",
                 "entry_hit", "color", "monochrome"}
        for binding in self.bindings:
            if binding.enabled and binding.action == "param":
                self.assertIn(binding.params.get("name"), known, binding.label)

    #: Every address a real Synesthesia was observed sending, captured off the
    #: wire with `osc-watch` rather than taken from the documentation - which
    #: names the uniforms but not the addresses, and which this list does not
    #: match. The scheme is `/audio/<family>/<band>`, so the band comes *after*
    #: the family and the first version of this map, globbing `*bass*level*`,
    #: matched nothing at all.
    #:
    #: The whole list rather than a sample, because the overlaps are the point:
    #: `*/level/mid` must not also take `/audio/level/midhigh`, `*/bpm/bpm`
    #: must not take `bpmconfidence` or the eight sine and triangle waves, and
    #: `*/beat/onbeat` must not take `randomonbeat`.
    UNIFORMS = tuple("/audio/" + name for name in (
        "level/all", "level/raw",
        "level/bass", "level/mid", "level/midhigh", "level/high",
        "hits/all", "hits/bass", "hits/mid", "hits/midhigh", "hits/high",
        "presence/all",
        "presence/bass", "presence/mid", "presence/midhigh", "presence/high",
        "time/all", "time/bass", "time/mid", "time/midhigh", "time/high",
        "time/curved",
        "beat/onbeat", "beat/randomonbeat", "beat/beattime",
        "bpm/bpm", "bpm/bpmconfidence", "bpm/bpmtwitcher",
        "bpm/bpmsin", "bpm/bpmsin2", "bpm/bpmsin4", "bpm/bpmsin8",
        "bpm/bpmtri", "bpm/bpmtri2", "bpm/bpmtri4", "bpm/bpmtri8",
        "energy/intensity",
    ))

    def test_each_binding_takes_exactly_the_uniform_it_names(self):
        """The failure this map's `exclude` lists exist to prevent: several
        sources arriving at one destination, none of them wrong on its own.

        `*level*` takes BassLevel too unless told not to; `*mid*` takes
        MidHigh; `*bpm` takes BPMConfidence and the six BPM waves. A glob
        cannot say "not", which is what `exclude` is for - and a binding that
        silently takes four uniforms is a knob driven by four sources at once.
        """
        for binding in self.bindings:
            if not binding.enabled:
                continue
            taken = [address for address in self.UNIFORMS if binding.matches(address)]
            self.assertEqual(len(taken), 1, f"{binding.label} takes {taken}")

    def test_no_uniform_is_taken_by_two_bindings(self):
        """The same failure from the other side: one address arriving at two
        destinations. Harmless for two levels, not harmless for two writers on
        one channel of the bus - that is whichever spoke last."""
        for address in self.UNIFORMS:
            takers = [b.label for b in self.bindings
                      if b.enabled and b.matches(address)]
            self.assertLessEqual(len(takers), 1, f"{address} taken by {takers}")


class OscCommand(unittest.TestCase):
    """The refusals, which are the only paths that reach no hardware at all."""

    def run_cli(self, *argv) -> int:
        from eclipse_dmx.cli import main

        stderr, sys.stderr = sys.stderr, io.StringIO()
        try:
            return main(list(argv))
        finally:
            sys.stderr = stderr

    def test_needs_a_config_or_a_test(self):
        self.assertEqual(self.run_cli("osc"), 1)

    def test_a_malformed_address_is_a_sentence_not_a_traceback(self):
        self.assertEqual(self.run_cli("osc", "--test", "--address", "127.0.0.1:nope"), 1)


class TheScenes(unittest.TestCase):
    """The visualiser scenes, against what the sender assumes about them.

    A scene is half of this feature rather than an attachment to it: the control
    it exposes *is* the address eclipse-dmx aims at, and the sender cannot tell
    when that stops being true. Nothing here needs Synesthesia - these are the
    assumptions that can be checked on paper, and they are exactly the ones that
    break silently.
    """
    SCENES = sorted((DESKTOP / "scenes").glob("*.synScene"))

    def manifest(self, scene: Path) -> dict:
        # Strict json, deliberately not the config loader's comment-tolerant
        # one: Synesthesia's parser is strict, and a scene this suite accepts
        # and the app rejects is worse than no test.
        return json.loads((scene / "scene.json").read_text(encoding="utf-8"))

    def test_there_are_scenes(self):
        self.assertTrue(self.SCENES, "no .synScene folders in desktop/scenes")

    def test_the_first_colour_control_is_the_rig_colour(self):
        # The sender's default target is positional - color/1 is the first
        # colour control in declaration order, not one named "1". Reordering a
        # CONTROLS array therefore repoints every sender at once, with no error
        # anywhere: the scene runs, the packets go out, nothing moves.
        for scene in self.SCENES:
            with self.subTest(scene=scene.name):
                colours = [control["NAME"] for control in self.manifest(scene)["CONTROLS"]
                           if control["TYPE"].split()[0] == "color"]
                if not colours:
                    # Allowed: a pure diagnostic scene takes nothing from the
                    # rig and has nothing for the sender to aim at. The contract
                    # is about scenes that do, not about every scene here.
                    continue

                # And the same name in every scene, so the stable by-name
                # address works on all of them. Synesthesia lowercases control
                # names and drops underscores for OSC, which is what makes
                # `rig_color` reachable as /controls/scene/rigcolor.
                self.assertEqual(colours[0], "rig_color")

    def test_every_control_is_read_by_the_shader(self):
        # A control that nothing reads is a knob that does nothing, which on a
        # desk mid-set is indistinguishable from a broken one.
        for scene in self.SCENES:
            with self.subTest(scene=scene.name):
                shader = (scene / "main.glsl").read_text(encoding="utf-8")
                for control in self.manifest(scene)["CONTROLS"]:
                    self.assertIn(control["NAME"], shader, control["NAME"])

    def test_every_control_says_what_it_does(self):
        for scene in self.SCENES:
            with self.subTest(scene=scene.name):
                for control in self.manifest(scene)["CONTROLS"]:
                    self.assertTrue(control.get("DESCRIPTION", "").strip(),
                                    f"{control['NAME']} has no DESCRIPTION")

    def test_every_scene_declares_a_gpu_tier(self):
        # Found the hard way: three scenes that compiled clean, loaded into the
        # browser, and rendered black. 33 of the 36 scenes on the machine -
        # every one the app ships, every marketplace scene, and every scene the
        # app's own editor writes - declare GPU. The three that did not were
        # ours. Nothing reports this: the tile appears, the shader is fine, the
        # screen is black.
        #
        # 0 is what the app's own new-scene template uses, and what these are:
        # the cheapest tier, no raymarching, no big loops.
        for scene in self.SCENES:
            with self.subTest(scene=scene.name):
                self.assertIn("GPU", self.manifest(scene),
                              "no GPU tier - the app loads this and renders black")

    def test_the_thumbnail_is_there(self):
        for scene in self.SCENES:
            with self.subTest(scene=scene.name):
                manifest = self.manifest(scene)
                self.assertTrue((scene / manifest["IMAGE_PATH"]).is_file(),
                                f"IMAGE_PATH names {manifest['IMAGE_PATH']}, which is not there")

    def test_the_shader_has_an_entry_point(self):
        for scene in self.SCENES:
            with self.subTest(scene=scene.name):
                shader = (scene / "main.glsl").read_text(encoding="utf-8")
                self.assertIn("vec4 renderMain(", shader)


class TheRig(unittest.TestCase):
    """Ten U'King Par 36 in 7-channel mode, addressed from 1."""

    def test_loads_clean(self):
        config = Config.load(RIG)
        self.assertEqual(config.addressing, "one")
        self.assertEqual(config.validate(), [])

    def test_fixtures_sit_one_footprint_apart(self):
        config = Config.load(RIG)
        self.assertEqual(
            [f.start_channel for f in config.fixtures],
            [1, 8, 15, 22, 29, 36, 43, 50, 57, 64],
        )

    def test_seven_channels_each_and_nothing_overlaps(self):
        config = Config.load(RIG)
        for fixture in config.fixtures:
            used = fixture.used_channels()
            self.assertEqual(len(used), 7, f"{fixture.name} claims {sorted(used)}")
            self.assertEqual(max(used) - min(used), 6)

        every = [channel for f in config.fixtures for channel in f.used_channels()]
        self.assertEqual(len(every), len(set(every)))
        self.assertEqual(max(every), 70)

    def test_mode_channels_are_parked(self):
        """The bit that decides whether a cheap par obeys you at all."""
        first = Config.load(RIG).fixtures[0]
        self.assertEqual(first.static_channels, {5: 0, 6: 0, 7: 0})
        self.assertEqual(first.dimmer_channel, 1)


class Addressing(unittest.TestCase):
    """Zero-based configs must resolve to the same slots the executable uses."""

    def test_zero_based_addresses_shift_by_one(self):
        config = Config.from_dict({
            "addressing": "zero",
            "fixtures": [{"profile": "uking_par36", "name": "par", "address": 0, "count": 10}],
        })
        self.assertEqual(
            [f.start_channel for f in config.fixtures],
            [1, 8, 15, 22, 29, 36, 43, 50, 57, 64],
        )

    def test_display_channel_round_trips(self):
        config = Config.from_dict({
            "addressing": "zero",
            "fixtures": [{"profile": "uking_par36", "name": "par", "address": 0, "count": 10}],
        })
        self.assertEqual(
            [config.display_channel(f.start_channel) for f in config.fixtures],
            [0, 7, 14, 21, 28, 35, 42, 49, 56, 63],
        )

    def test_same_rig_either_numbering(self):
        """The whole point: the two numberings describe identical hardware."""
        one = Config.from_dict({
            "addressing": "one",
            "fixtures": [{"profile": "uking_par36", "name": "par", "address": 1, "count": 10}],
        })
        zero = Config.from_dict({
            "addressing": "zero",
            "fixtures": [{"profile": "uking_par36", "name": "par", "address": 0, "count": 10}],
        })
        self.assertEqual(
            [f.used_channels() for f in one.fixtures],
            [f.used_channels() for f in zero.fixtures],
        )

    def test_absent_dimmer_is_no_dimmer(self):
        config = Config.from_dict({
            "addressing": "zero",
            "fixtures": [{"name": "a", "start_channel": 0, "channels": "rgb"}],
        })
        self.assertEqual(config.fixtures[0].dimmer_channel, 0)

    def test_explicit_zero_dimmer_is_the_first_slot(self):
        config = Config.from_dict({
            "addressing": "zero",
            "fixtures": [
                {"name": "a", "start_channel": 1, "channels": "rgb", "dimmer_channel": 0}
            ],
        })
        self.assertEqual(config.fixtures[0].dimmer_channel, 1)

    def test_static_channels_shift_too(self):
        config = Config.from_dict({
            "addressing": "zero",
            "fixtures": [
                {"name": "a", "start_channel": 0, "channels": "rgb",
                 "static_channels": {"3": 7}}
            ],
        })
        self.assertEqual(config.fixtures[0].static_channels, {4: 7})

    def test_bad_addressing_rejected(self):
        with self.assertRaises(ConfigError):
            Config.from_dict({"addressing": "middle", "fixtures": []})

    def test_round_trip_does_not_shift_twice(self):
        """A zero-based config written back out must not move."""
        loaded = Config.from_dict({
            "addressing": "zero",
            "fixtures": [{"profile": "uking_par36", "name": "par", "address": 0, "count": 10}],
        })
        again = Config.from_dict(loaded.to_dict())
        self.assertEqual(
            [f.used_channels() for f in loaded.fixtures],
            [f.used_channels() for f in again.fixtures],
        )


class CoordSpans(unittest.TestCase):
    """Relic patterns need a coordinate space, and it has to survive a file."""

    def test_unset_by_default(self):
        """Unset means the pattern's own frame wins, which is what we want.

        A number here would pin every pattern to one space, and the obelisk's
        looks and the jacket's want quite different ones.
        """
        pattern = Config().pattern
        self.assertEqual(
            (pattern.coord_origin_x, pattern.coord_origin_y,
             pattern.coord_span_x, pattern.coord_span_y),
            (None, None, None, None),
        )

    def test_unset_spans_are_not_written(self):
        written = Config().pattern.to_dict()
        self.assertNotIn("coord_span_x", written)
        self.assertNotIn("coord_origin_x", written)

    def test_round_trips(self):
        config = Config()
        config.pattern.coord_span_x = 4.0
        config.pattern.coord_span_y = 12.0
        again = Config.from_dict(config.to_dict())
        self.assertEqual(again.pattern.coord_span_x, 4.0)
        self.assertEqual(again.pattern.coord_span_y, 12.0)

    def test_zero_span_rejected(self):
        config = Config()
        config.add_rgb_bank(count=2)
        config.pattern.coord_span_y = 0.0
        with self.assertRaises(ConfigError):
            config.validate()


class TheObelisk(unittest.TestCase):
    """The sculpture as a device this desk can patch patterns for.

    Not a DMX rig: 344 pixels at three channels each is 1032, twice a universe,
    and legal exactly because nothing is putting it on a DMX wire.
    """
    def test_loads_clean(self):
        config = Config.load(OBELISK)
        self.assertEqual(config.validate(), [])
        self.assertEqual(len(config.fixtures), OBELISK_PIXELS)

    def test_pixels_are_consecutive_with_no_gaps(self):
        config = Config.load(OBELISK)
        self.assertEqual(
            [f.start_channel for f in config.fixtures],
            [1 + index * 3 for index in range(OBELISK_PIXELS)],
        )
        self.assertEqual(config.highest_channel(), OBELISK_PIXELS * 3)

    def test_reaching_past_a_universe_is_not_a_warning_here(self):
        """The rig is the buffer. Only a DMX wire has 512 slots."""
        config = Config.load(OBELISK)
        self.assertGreater(config.highest_channel(), 512)
        self.assertEqual(config.validate(), [])

    def test_the_same_patch_on_a_dmx_wire_does_warn(self):
        """And the moment it is one, the overflow is real again."""
        config = Config.load(OBELISK)
        config.device.type = "enttec_open"
        with self.assertRaises(ConfigError):
            config.validate()

    def test_coordinates_are_the_sculptures_own(self):
        """x is which strip, y is how far up - the space the looks were tuned in.

        These are the coordinates ObeliskIO::init builds on the sculpture. The
        down strips running 43..1 against the up strips' 0..42 is the firmware's
        own off-by-one, reproduced on purpose.
        """
        config = Config.load(OBELISK)
        self.assertEqual(config.coord_space, "literal")

        positions = [f.position for f in config.fixtures]
        for strip in range(OBELISK_STRIPS):
            run = positions[strip * OBELISK_SIDE_LENGTH:(strip + 1) * OBELISK_SIDE_LENGTH]
            self.assertTrue(all(point[0] == float(strip) for point in run))

            # Up and down runs cover the *same* range, because the pixels
            # physically line up. The down ones used to start a unit high; see
            # the note in devices/obelisk.json.
            if strip % 2 == 0:
                self.assertEqual([point[1] for point in run],
                                 [float(y) for y in range(OBELISK_SIDE_LENGTH)])
            else:
                self.assertEqual([point[1] for point in run],
                                 [float(OBELISK_SIDE_LENGTH - 1 - y)
                                  for y in range(OBELISK_SIDE_LENGTH)])

    def test_literal_positions_survive_a_round_trip(self):
        """Unlike addressing, this is not resolved away on load."""
        again = Config.from_dict(Config.load(OBELISK).to_dict())
        self.assertEqual(again.coord_space, "literal")
        self.assertEqual(
            [f.position for f in again.fixtures],
            [f.position for f in Config.load(OBELISK).fixtures],
        )

    def test_a_bad_coord_space_is_rejected(self):
        with self.assertRaises(ConfigError):
            Config.from_dict({"coord_space": "sideways", "fixtures": []})


class TheObeliskOnAWire(unittest.TestCase):
    """The same sculpture, patched to its own USB cable instead of the viewer."""

    def test_loads_clean(self):
        config = Config.load(OBELISK_USB)
        self.assertEqual(config.validate(), [])
        self.assertEqual(config.device.type, "relic_usb")

    def test_the_patch_is_the_same_sculpture(self):
        """Two files describing one object. They must not drift."""
        wired = Config.load(OBELISK_USB)
        preview = Config.load(OBELISK)

        self.assertEqual(
            [(f.name, f.start_channel, f.position) for f in wired.fixtures],
            [(f.name, f.start_channel, f.position) for f in preview.fixtures],
        )
        self.assertEqual(wired.coord_space, preview.coord_space)

    def test_a_relic_is_not_bound_by_a_universe(self):
        config = Config.load(OBELISK_USB)
        self.assertGreater(config.highest_channel(), 512)
        self.assertEqual(config.validate(), [])

    def test_cue_mode_is_a_device_type(self):
        config = Config.load(OBELISK_USB)
        config.device.type = "relic_usb_cue"
        self.assertEqual(config.validate(), [])

    def test_frame_rate_stays_under_what_the_relic_can_draw(self):
        """344 WS2812s are 10.3ms of show() and the relic's loop sleeps 30ms.

        Sending faster does not draw faster; the extra frames are read and
        dropped a tick later. This is a note-to-self with teeth.
        """
        self.assertLessEqual(Config.load(OBELISK_USB).device.fps, 30.0)


class OverSsh(unittest.TestCase):
    """A remote show is the same protocol through ssh's pipes.

    None of this needs a host or the binary: it checks what would be run, not
    that it ran. The far end is exercised by pointing a viewer at the pi.
    """
    def _remote(self, config=SCANNER, **kwargs):
        return ShowController(config, remote="scanner-pi", autostart=False,
                              on_frame=lambda f: None, **kwargs)

    def test_needs_no_local_binary(self):
        show = self._remote(executable="/definitely/not/here")
        self.assertIsNone(show.executable)

    def test_runs_ssh_in_batch_mode(self):
        args = self._remote()._build_args()
        self.assertEqual(args[0], "ssh")
        self.assertIn("BatchMode=yes", args)
        self.assertEqual(args[-2], "scanner-pi")

    def test_config_travels_relative_to_desktop(self):
        """The same checkout at both ends names the file the same way, whatever
        the checkouts are called and whichever slash the desk uses."""
        line = self._remote()._build_args()[-1]
        self.assertIn("--config config/scanner.json", line)
        self.assertNotIn("\\", line)

    def test_a_path_only_the_host_has_goes_through_as_written(self):
        line = self._remote(config="/home/jakee/rig.json")._build_args()[-1]
        self.assertIn("--config /home/jakee/rig.json", line)

    def test_flags_are_quoted_for_a_posix_shell(self):
        line = self._remote(remote_command="./desk.sh", midi="loopMIDI Port")._build_args()[-1]
        self.assertTrue(line.startswith("./desk.sh "))
        self.assertIn("--midi 'loopMIDI Port'", line)
        self.assertIn("--emit-frames", line)

    def test_remote_command_can_be_named(self):
        line = self._remote(remote_command="eclipse-dmx")._build_args()[-1]
        self.assertTrue(line.startswith("eclipse-dmx --config"))

    def test_a_config_object_is_refused(self):
        """A file written here is not a file there."""
        config = Config()
        config.add_bank("uking_par36", count=2, address=1)
        with self.assertRaises(ConfigError):
            self._remote(config=config)

    def test_local_shows_are_unchanged(self):
        try:
            show = ShowController(SCANNER, autostart=False, on_frame=lambda f: None)
        except BinaryNotFoundError as error:
            raise unittest.SkipTest(str(error))
        args = show._build_args()
        self.assertNotEqual(args[0], "ssh")
        self.assertEqual(args[1:3], ["--config", str(SCANNER)])


class TheScannerStage(unittest.TestCase):
    """config/scanner_stage.json: the scanner's show with the DMX truss in it."""

    def setUp(self):
        self.config = Config.load(DESKTOP / "config" / "scanner_stage.json")

    def test_three_devices_ring_first(self):
        """The ring first, as the game's own inline config has it; the truss
        last, so adding it moved nothing in the frame stream."""
        self.assertEqual([device.name for device in self.config.devices],
                         ["scanner_ring", "obelisk", "pars"])

    def test_the_truss_stands_on_the_floor_centred_on_the_obelisk(self):
        """On kStageBottom (0) with the obelisk's lowest run, one face wide
        (two strips) and centred on x 3.5 - so a wave starts on both, and the
        truss sees what one face of the sculpture sees."""
        pars = self.config.devices[2]
        self.assertEqual(pars.output.type, "enttec_open")
        # y fitted to 0: a normalized device's local coordinates run up the
        # frame's diagonal, and only a zero fit flattens that to a row
        self.assertEqual(pars.placement.fit, [2.0, 0.0])
        # at the ring's height, a little above its centre
        self.assertEqual(pars.placement.offset[1], 16.0)
        centre = pars.placement.offset[0] + pars.placement.fit[0] / 2.0
        self.assertAlmostEqual(centre, 3.5)

    def test_it_is_the_scanners_show(self):
        self.assertEqual(self.config.pattern.name, "scanner")
        self.assertEqual(self.config.validate(strict_overlap=False), [])


class TheUvLayer(ShowTest):
    """A second pattern on one named fixture, over the show."""

    def test_the_stage_config_declares_it(self):
        config = Config.load(DESKTOP / "config" / "scanner_stage.json")
        self.assertEqual(config.layers, [
            # "over" is the default and what this layer has always done: the UV
            # par is off or flashing, and what the show wanted on it is beside
            # the point. See TheAudioBus for the additive case.
            {"name": "uv", "fixtures": ["pars/uv"], "pattern": "uv", "state": "off",
             "blend": "over"}])
        # and it survives a round trip
        again = Config.from_dict(config.to_dict())
        self.assertEqual(again.layers, config.layers)

    def test_a_layer_needs_a_pattern_and_fixtures(self):
        with self.assertRaises(ConfigError):
            Config.from_dict({"fixtures": [], "layers": [{"name": "x", "fixtures": ["a"]}]})
        with self.assertRaises(ConfigError):
            Config.from_dict({"fixtures": [], "layers": [{"name": "x", "pattern": "uv"}]})

    def test_the_wrapper_reads_a_layers_announcements(self):
        """The show's own lines, prefixed - read into a LayerView with the
        same shape the show's look has, and nothing lands on the show's."""
        show = ShowController(SCANNER, remote="nowhere", autostart=False)
        for line in [
            "PARAMS scanner scan_idle",
            "PARAM cycle_time f 2 0.5 8",
            "LAYER uv FIXTURES 389",
            "LAYER uv PATTERN uv",
            "LAYER uv STATES off flash on",
            "LAYER uv STATE flash",
            "LAYER uv PARAMS uv flash",
            "LAYER uv PARAM decay f 0.3 0.01 3",
            "LAYER uv CURVE envelope 0:0 0.02:1:7 0.32:0",
            "READY",
        ]:
            show._handle_stdout(line)

        self.assertEqual([p.name for p in show.params], ["cycle_time"])
        self.assertEqual(show.params_revision, 1)

        uv = show.layers["uv"]
        self.assertEqual(uv.fixtures, [389])
        self.assertEqual(uv.pattern_name, "uv")
        self.assertEqual(uv.state_names, ["off", "flash", "on"])
        self.assertEqual(uv.current_state, "flash")
        self.assertEqual([p.name for p in uv.params], ["decay"])
        self.assertEqual(list(uv.curves), ["envelope"])
        self.assertEqual(uv.params_revision, 1)
        self.assertEqual(show.layers_revision, 1)

    def test_it_runs(self):
        """off is dark, on is white, flash carries the beat pulse's envelope,
        and the show's knobs are not the layer's."""
        import time

        frames = []
        show = self.running_show(DESKTOP / "config" / "scanner_stage.json", on_frame=frames.append, emit_rate=30.0)
        show.set_state("scan_idle")
        time.sleep(0.8)
        uv = show.layers["uv"]
        self.assertEqual(uv.fixtures, [389])
        self.assertEqual(frames[-1][389], (0, 0, 0))

        uv.set_state("on")
        time.sleep(0.5)
        self.assertEqual(frames[-1][389], (255, 255, 255))
        # the par beside it is still the show's - dark in scan_idle
        self.assertEqual(frames[-1][388], (0, 0, 0))

        uv.set_state("flash")
        time.sleep(0.2)
        self.assertIn("envelope", uv.curves)
        self.assertIsNotNone(uv.get_param("rate"))
        self.assertEqual(show.current_state, "scan_idle")
        self.assertIsNone(show.get_param("rate"))


class TheAudioBus(ShowTest):
    """Analysis in, as an ordinary parameter.

    The bus is the one place a number about the *sound* lives. Mixxx's VU notes
    fill three of its channels off the MIDI cable; Synesthesia's audio uniforms
    fill all of them over OSC. A modulation points an ordinary pattern knob at
    a channel, and nothing in any pattern knows which end filled it - which is
    the whole reason for putting a bus in the middle rather than letting looks
    read a cable.
    """

    # -- talking to the executable -----------------------------------------
    # Both of these are the harness's, bound here so the tests below read as
    # `self._protocol(...)` and default to this rig.

    @staticmethod
    def _protocol(script, frames=ANSWER_FRAMES, config=None):
        return run_show(script, frames=frames, config=config or SHOW)

    @staticmethod
    def _protocol_paced(script, pause=0.05, config=None):
        return run_show_paced(script, pause=pause, config=config or SHOW)

    # -- the channel list ---------------------------------------------------

    def test_the_channel_list_matches_the_executable(self):
        """AUDIO_CHANNELS is a mirror, and a mirror that drifts is worse than
        no mirror: a config would validate here and be refused there."""
        lines = self._protocol(["channels"])
        named = [line.split()[1] for line in lines if line.startswith("CHANNEL ")]
        self.assertEqual(tuple(named), AUDIO_CHANNELS)

    def test_every_channel_is_zero_and_dark_before_anything_feeds_it(self):
        """"Nothing is wired" has to be distinguishable from "the music is
        quiet", and both have to be safe. An unfed channel reads zero and not
        live, so a mod on it holds its knob at `low` rather than at whatever
        the last write left there."""
        lines = [line for line in self._protocol(["channels"])
                 if line.startswith("CHANNEL ")]
        self.assertEqual(len(lines), len(AUDIO_CHANNELS))
        for line in lines:
            _, name, value, live = line.split()
            self.assertEqual(float(value), 0.0, name)
            self.assertEqual(live, "-", name)

    def test_a_value_lands_on_its_channel_and_only_that_one(self):
        lines = [line for line in self._protocol(["audio bass_hits 0.75", "channels"])
                 if line.startswith("CHANNEL ")]
        readings = {line.split()[1]: line.split()[2:] for line in lines}
        self.assertEqual(float(readings["bass_hits"][0]), 0.75)
        self.assertEqual(readings["bass_hits"][1], "live")
        # its neighbours in the enum, untouched
        self.assertEqual(float(readings["mid_hits"][0]), 0.0)
        self.assertEqual(float(readings["hits"][0]), 0.0)

    def test_a_channel_that_is_not_one_is_refused_at_the_desk_too(self):
        lines = self._protocol(["audio bass_hit 1.0"])
        self.assertTrue(any(line.startswith("ERR") and "bass_hit" in line
                            for line in lines), lines)

    # -- the config ---------------------------------------------------------

    def test_the_show_declares_synesthesia_and_a_port(self):
        config = Config.load(SHOW)
        self.assertEqual(config.audio.source, "synesthesia")
        self.assertEqual(config.audio.port, 7000)
        self.assertTrue(config.audio.is_synesthesia)
        again = Config.from_dict(config.to_dict())
        self.assertEqual(again.audio.source, "synesthesia")

    def test_the_show_takes_the_beat_from_synesthesia_too(self):
        """One source owns the music. Having picked the app that is actually
        listening to it, its answer for where the beat is comes with."""
        config = Config.load(SHOW)
        self.assertTrue(config.audio.bpm)
        self.assertTrue(config.audio.owns_the_beat)
        self.assertTrue(Config.from_dict(config.to_dict()).audio.owns_the_beat)

    def test_a_mixxx_config_never_owns_the_beat_from_the_bus(self):
        """`bpm` only means anything alongside `source: synesthesia` - on
        mixxx the cable is the clock, which is what it always was."""
        self.assertFalse(AudioConfig(source="mixxx", bpm=True).owns_the_beat)
        self.assertFalse(AudioConfig(source="none", bpm=True).owns_the_beat)
        self.assertFalse(AudioConfig(source="synesthesia", bpm=False).owns_the_beat)

    # -- the clock ----------------------------------------------------------

    def test_the_desk_says_which_end_owns_the_beat(self):
        """`beat_from` is the configured owner; `src` inside the clock's own
        status is whoever last actually set it. They differ in exactly the case
        worth seeing at a venue - Mixxx playing and the rig not following it
        reads as beat_from=osc with src=internal, which says the cable is not
        wired to the clock rather than that the cable is dead."""
        line = next(one for one in self._protocol(["midi status"])
                    if one.startswith("MIDI-STATUS "))
        self.assertIn("beat_from=osc", line)
        self.assertIn("audio_from=synesthesia", line)

    def test_syn_bpm_sets_the_tempo(self):
        """50..220 across the channel's 0..1, which is the OSC binding's
        `range` run backwards - the bus is 0..1 everywhere."""
        # Restated a few times rather than once: the bus is read by the frame
        # loop, not the command handler, so this needs a frame to have run in
        # between - and one 0.05s gap is not reliably a frame on a loaded
        # machine. Several gives it several chances and still costs under a
        # second.
        lines = self._protocol_paced(["audio bpm 0.4706"] * 5 + ["midi status"],
                                     pause=0.08)
        line = next(one for one in lines if one.startswith("MIDI-STATUS "))
        reported = float(line.split("bpm=")[1].split()[0])
        self.assertAlmostEqual(reported, 130.0, delta=0.5)

    def test_an_empty_bus_leaves_the_tempo_where_it_was(self):
        """A channel nobody is filling reads zero, and zero here would mean
        50bpm. So nothing is done at all unless the channel is live: a link
        that never came up, or dropped mid-set, costs the tempo nothing and the
        show free-runs at the config's own bpm."""
        lines = self._protocol(["midi status"], frames=30)
        line = next(one for one in lines if one.startswith("MIDI-STATUS "))
        self.assertAlmostEqual(float(line.split("bpm=")[1].split()[0]), 128.0,
                               delta=0.5)
        self.assertIn("src=internal", line)

    def test_syn_onbeat_sets_the_phase_and_syn_bpm_still_owns_the_tempo(self):
        """Both channels come from one detector and both imply a tempo: one
        states it, the other implies it by when it fires. The stated one wins,
        because syn_OnBeat arrives over UDP through a rate-limited binding
        while syn_BPM is smoothed upstream - left to fight, the jittery one
        takes it within a few beats. See BeatClock::setTempoHeld.

        Driven here at a beat spacing that deliberately disagrees with the
        stated tempo, which is the only way to tell the two apart.
        """
        script = []
        for _ in range(16):
            script += ["audio bpm 0.4706", "audio beat 1.0", "audio beat 0.0"]
        script.append("midi status")

        # Three lines a beat at 0.09s is ~0.27s between beats, about 220bpm:
        # clearly not the 130 being stated, and still inside the 30..300 the
        # clock will entertain at all. Faster than kMaxBpm and markBeat drops
        # the beats as one beat said twice, which would make this pass whether
        # the tempo were held or not.
        lines = self._protocol_paced(script, pause=0.09)
        line = next(one for one in lines if one.startswith("MIDI-STATUS "))

        # The stated tempo, not the one the edges imply.
        self.assertAlmostEqual(float(line.split("bpm=")[1].split()[0]), 130.0,
                               delta=0.5)
        # and the beats did land: the phase came from syn_OnBeat
        self.assertIn("src=osc", line)

    def test_a_config_with_no_audio_block_is_mixxx(self):
        """Which is what every config written before the bus existed is, and
        they must go on behaving exactly as they did."""
        config = Config.load(RIG)
        self.assertEqual(config.audio.source, "mixxx")
        self.assertFalse(config.audio.is_synesthesia)

    def test_an_unknown_source_or_port_is_refused(self):
        with self.assertRaises(ConfigError):
            AudioConfig(source="ableton").validate()
        with self.assertRaises(ConfigError):
            AudioConfig(port=0).validate()

    # -- mods ---------------------------------------------------------------

    def test_the_short_and_long_spellings_mean_the_same_thing(self):
        """The object form is the array form with every default taken. A rig of
        one-line hit layers should not have to spell out five fields."""
        short = Config.from_dict({
            "fixtures": [{"profile": "rgb3", "address": 1}],
            "mods": {"level": "bass_hits"},
        })
        long = Config.from_dict({
            "fixtures": [{"profile": "rgb3", "address": 1}],
            "mods": [{"param": "level", "channel": "bass_hits",
                      "low": 0.0, "high": 1.0, "slew": 0.0}],
        })
        self.assertEqual(short.mods[0]["param"], "level")
        self.assertEqual(short.mods[0]["channel"], "bass_hits")
        self.assertEqual(long.mods, [{"param": "level", "channel": "bass_hits",
                                      "enabled": True,
                                      "low": 0.0, "high": 1.0, "slew": 0.0}])

    def test_a_channel_that_is_not_one_is_refused(self):
        """The one failure on this path that is otherwise completely silent: a
        mod on a misspelled channel binds to nothing and simply never fires,
        which looks exactly like a visualiser that is not sending."""
        with self.assertRaises(ConfigError) as caught:
            Config.from_dict({
                "fixtures": [{"profile": "rgb3", "address": 1}],
                "mods": {"level": "bass_hit"},   # bass_hits
            })
        self.assertIn("bass_hit", str(caught.exception))

        with self.assertRaises(ConfigError):
            Config.from_dict({
                "fixtures": [{"profile": "rgb3", "address": 1}],
                "layers": [{"name": "x", "fixtures": ["a"], "pattern": "solid",
                            "mods": {"level": "nope"}}],
            })

    def test_a_blend_that_is_not_over_or_add_is_refused(self):
        with self.assertRaises(ConfigError):
            Config.from_dict({
                "fixtures": [{"profile": "rgb3", "address": 1}],
                "layers": [{"name": "x", "fixtures": ["a"], "pattern": "solid",
                            "blend": "screen"}],
            })

    # -- the shipped map ----------------------------------------------------

    def test_the_map_fills_every_channel_the_visualiser_has(self):
        """One binding per channel, authored once. This is the difference the
        bus buys: a binding onto a *knob* has to know what look is running and
        dies when the look changes, so it is rewritten per show; a binding onto
        a channel knows neither and is written once."""
        bindings = osc_input.BindingSet.load(
            DESKTOP / "config" / "oscmaps" / "synesthesia.json")

        filled = [b.params["channel"] for b in bindings.bindings
                  if b.action == "audio" and b.enabled]

        # No channel is fed twice - two bindings on one channel is whichever
        # spoke last, and there is no reading of that a desk could show.
        self.assertEqual(sorted(filled), sorted(set(filled)))

        for channel in filled:
            self.assertIn(channel, AUDIO_CHANNELS)

        # Everything except the two Mixxx-only meters, which no visualiser
        # sends and which stay dark on this source by design.
        self.assertEqual(set(AUDIO_CHANNELS) - set(filled),
                         {"level_instant", "level_meter"})

        # `/audio/level/raw` is deliberately unbound: it is the unsmoothed
        # level beside `/audio/level/all`, and binding both would put two
        # writers on `level` - whichever spoke last.
        self.assertFalse([b for b in bindings.bindings
                          if b.enabled and b.matches("/audio/level/raw")])

    def test_the_tempo_goes_through_the_bus_and_not_around_it(self):
        """There is one route to the rig's tempo and it is the `bpm` channel.

        The map used to carry a second binding straight onto the `bpm` action,
        which set the clock directly. Two ways to set one number is the
        confusion the whole `source` switch exists to remove - so the channel
        is the route, and `audio.bpm` is what decides whether anything reads it.
        """
        bindings = osc_input.BindingSet.load(
            DESKTOP / "config" / "oscmaps" / "synesthesia.json")
        self.assertFalse([b for b in bindings.bindings if b.action == "bpm"])
        self.assertTrue([b for b in bindings.bindings
                         if b.action == "audio" and b.params["channel"] == "bpm"])

    def test_the_bpm_channel_arrives_scaled(self):
        """The bus is 0..1 everywhere, tempo included: scaled on the way in so
        that nothing downstream has to know one channel is in different units."""
        binding = next(b for b in osc_input.BindingSet.load(
            DESKTOP / "config" / "oscmaps" / "synesthesia.json").bindings
            if b.action == "audio" and b.params["channel"] == "bpm")
        self.assertEqual((binding.low, binding.high), (50.0, 220.0))
        self.assertAlmostEqual(binding.scale(135.0), (135.0 - 50.0) / 170.0)

    def test_the_audio_action_streams_a_channel_and_a_value(self):
        """It does not touch a knob: it writes a number onto the bus, and
        whatever is pointed at that channel picks it up next frame. Streamed,
        because a channel filled at frame rate cannot afford a round trip per
        value - and the executable answers nothing for the same reason."""
        show = OscInputActions.FakeShow()
        spec = midi_map.ACTIONS["audio"]
        spec.run(midi_map.ActionContext(show=show),
                 midi_map.coerce_params(spec, {"channel": "bass_hits"}), 0.5)
        self.assertEqual(show.calls, [("command", "audio bass_hits 0.5000", False)])

    # -- the three hit layers -----------------------------------------------

    def test_the_show_declares_no_layers_at_all(self):
        """The invariant that replaced three always-on ones.

        An additive layer rides over *whatever* is running. The argument for
        leaving these patched was that a layer sitting at black adds nothing -
        which holds exactly until the channels are being fed, and then every
        cue in the show is itself plus red, green and blue, the obelisk is 344
        pixels of jittering yellow-green, and the audio meter reports the
        number it exists to report plus a layer. A permanent overlay across
        three devices is an implicit global effect and the rig should not have
        one.

        They live in config/audio_layers_test.json now, where switching them
        on is something done on purpose.
        """
        self.assertEqual(Config.load(SHOW).layers, [])

    def test_the_bench_rig_declares_three_additive_hit_layers(self):
        config = Config.load(LAYERS)
        by_name = {layer["name"]: layer for layer in config.layers}
        self.assertEqual(sorted(by_name), ["hit_obelisk", "hit_pars", "hit_ring"])

        # One section each, one colour each, one band each, in frequency order
        # up the room: kick at the bottom, hi-hat at the top.
        expected = {
            "hit_ring":    ("scanner_ring/*", "#ff0000", "bass_hits"),
            "hit_obelisk": ("obelisk/*",      "#00ff00", "mid_hits"),
            # midhigh_hits, not high_hits: /audio/hits/high was measured flat
            # at zero on the real app while every band around it moved. See the
            # note in the config.
            "hit_pars":    ("pars/par_*",     "#0000ff", "midhigh_hits"),
        }
        for name, (fixtures, color, channel) in expected.items():
            layer = by_name[name]
            self.assertEqual(layer["fixtures"], [fixtures], name)
            self.assertEqual(layer["blend"], "add", name)
            self.assertEqual(layer["color"], color, name)
            self.assertEqual(layer["mods"],
                             [{"param": "level", "channel": channel, "enabled": True}],
                             name)

    def test_the_truss_layer_leaves_the_uv_par_alone(self):
        """pars/par_* and not pars/*: the UV is the eleventh fixture on the
        same cable and it is a blacklight, so a colour means nothing to it and
        only the level would land - a UV unit strobing on every hi-hat."""
        config = Config.load(LAYERS)
        layer = next(l for l in config.layers if l["name"] == "hit_pars")
        self.assertNotIn("pars/*", layer["fixtures"])

    def test_a_layer_resolves_to_its_section_and_nothing_else(self):
        """The wildcard, against the real rig: 35 ring pixels, 344 obelisk, and
        10 pars without the UV on the end of them."""
        show = self.running_show(LAYERS, midi="")
        time.sleep(0.6)
        self.assertEqual(len(show.layers["hit_ring"].fixtures), 35)
        self.assertEqual(len(show.layers["hit_obelisk"].fixtures),
                         OBELISK_PIXELS)
        self.assertEqual(len(show.layers["hit_pars"].fixtures), 10)

    def test_an_unfed_additive_layer_is_invisible(self):
        """The guarantee that makes these safe to leave patched.

        An additive layer sitting at black adds nothing, so the hit layers ride
        over every cue rather than only the one written for them - and a night
        with no visualiser costs the hits and nothing else. Checked on
        beat_pulse, whose white hit would show any tint immediately.
        """
        frames = []
        show = self.running_show(SHOW, pattern="audio", midi="", bpm=120.0,
                              on_frame=frames.append, emit_rate=40.0)
        # The bus opens on `level`, so the flash has to be asked for: it is
        # a cue on this list while the show is empty - see beatPulseState.
        show.set_state("beat_pulse")
        time.sleep(2.0)

        peaks = [f for f in frames if max(f[0]) > 200]
        self.assertTrue(peaks, "the rig never reached full brightness")
        for frame in peaks:
            for red, green, blue in frame:
                self.assertEqual((red, green, blue), (red, red, red))

    def test_a_fed_channel_drives_its_layer(self):
        """End to end: a value onto the bus, through a mod, into an ordinary
        knob, out as light on one section and not the others."""
        frames = []
        show = self.running_show(LAYERS, midi="",
                              on_frame=frames.append, emit_rate=40.0)
        time.sleep(0.5)

        ring = show.layers["hit_ring"].fixtures[0]
        obelisk = show.layers["hit_obelisk"].fixtures[0]
        par = show.layers["hit_pars"].fixtures[0]

        # The bus decays a reading it has not heard from in 0.8s - a
        # dead-man's switch, not an effect - so this is held up rather
        # than set once.
        deadline = time.time() + 1.2
        while time.time() < deadline:
            show.command("audio bass_hits 1.0", expect_reply=False)
            time.sleep(0.05)

        frame = frames[-1]
        self.assertGreater(frame[ring][0], 200, "the ring did not go red")
        self.assertEqual(frame[ring][1], 0)
        self.assertEqual(frame[ring][2], 0)

        # The other two sections were never fed, so they are still dark.
        self.assertEqual(frame[obelisk], (0, 0, 0))
        self.assertEqual(frame[par], (0, 0, 0))

    def test_the_config_declares_the_mods_the_desk_reports(self):
        lines = [line for line in self._protocol(["mods"], config=LAYERS) if " MOD " in line]
        self.assertIn("LAYER hit_ring MOD level bass_hits 0 1 0 ok", lines)
        self.assertIn("LAYER hit_obelisk MOD level mid_hits 0 1 0 ok", lines)
        self.assertIn("LAYER hit_pars MOD level midhigh_hits 0 1 0 ok", lines)

    def test_a_mod_can_be_repointed_without_a_restart(self):
        """A config declares the opening state; the desk retargets it. Which is
        the difference between a rig you tune and a rig you edit and relaunch -
        and relaunching is not available in front of a room."""
        lines = self._protocol([
            "layer hit_ring mod level high_hits",
            "layer hit_ring mods",
        ], config=LAYERS)
        mods = [line for line in lines if line.startswith("LAYER hit_ring MOD ")]
        self.assertEqual(mods, ["LAYER hit_ring MOD level high_hits 0 1 0 ok"])

    def test_one_knob_has_one_driver(self):
        """Repointing replaces rather than stacks: two modulations on one
        property is a race whose winner is whichever ran last, and there is no
        reading of that a desk could show."""
        lines = self._protocol([
            "layer hit_ring mod level high_hits",
            "layer hit_ring mod level mid_hits",
            "layer hit_ring mods",
        ], config=LAYERS)
        mods = [line for line in lines if line.startswith("LAYER hit_ring MOD ")]
        self.assertEqual(len(mods), 1, mods)
        self.assertIn("mid_hits", mods[0])

    def test_a_mod_can_be_taken_off(self):
        lines = self._protocol([
            "layer hit_ring mod level off",
            "layer hit_ring mods",
        ], config=LAYERS)
        self.assertFalse([line for line in lines
                          if line.startswith("LAYER hit_ring MOD ")], lines)

    def test_a_mod_says_when_its_knob_is_not_there(self):
        """A mod survives a cue change - that is the point, the bus keeps
        driving as the show moves - but a knob does not, and a mod pointing at
        a knob the running look has never had is otherwise completely silent,
        because a per-frame writer cannot complain sixty times a second."""
        lines = self._protocol(["mod not_a_knob bass", "mods"])
        self.assertTrue(any(line.startswith("MOD not_a_knob bass")
                            and line.endswith("no-param") for line in lines), lines)

    def test_a_mod_onto_a_colour_is_refused(self):
        """A colour cannot come from one number, and half-writing one is worse
        than refusing - the same reason PropertyBag::set leaves colours alone."""
        lines = self._protocol(["layer hit_ring mod color bass"], config=LAYERS)
        self.assertTrue(any(line.startswith("ERR") and "colour" in line
                            for line in lines), lines)


class TheBindingKeepsTheBusAlive(unittest.TestCase):
    """The regression that shipped: a steady value vanishing off the bus.

    Two mechanisms, each right on its own. A binding suppresses an unchanged
    value - "a level holding still says nothing" - which was correct when a
    binding drove a *knob*, because a knob that has been set stays set. The bus
    instead fades any channel nobody has restated in 0.8s, which is a
    dead-man's switch: without it a dropped link leaves the rig lit at whatever
    the music was doing when it went.

    Together, with no keepalive, they delete the value. The channel is
    suppressed at this end, goes stale at the other, and the look driven from
    it fades to nothing while the music is still playing.

    It got through because the end-to-end test fed the bus with `show.command`
    directly, which is not the path a set uses - so the binding's rate limiting
    was never in the loop. These go through the binding.
    """
    def binding(self, channel="bass_hits"):
        return next(b for b in osc_input.BindingSet.load(
            DESKTOP / "config" / "oscmaps" / "synesthesia.json").bindings
            if b.action == "audio" and b.params["channel"] == channel)

    def test_a_steady_value_is_still_restated(self):
        binding = self.binding()
        now = time.monotonic()
        fired = [step for step in range(40)
                 if binding.fires_on(1.0, now + step * 0.05) is not None]

        self.assertGreater(len(fired), 1, "a held value was sent once and never again")

        # and the gaps stay inside the bus's hold, so it never begins to decay
        from eclipse_dmx.osc_input import Binding
        gaps = [(b - a) * 0.05 for a, b in zip(fired, fired[1:])]
        self.assertTrue(gaps)
        self.assertLessEqual(max(gaps), 0.35,
                             "a gap longer than AudioLevel::kHoldFor lets the value sag")

    def test_it_still_says_far_less_than_it_hears(self):
        """The saving the suppression exists for is not given up: sixty
        messages a second become five, not sixty."""
        binding = self.binding()
        now = time.monotonic()
        heard = 120                       # two seconds at 60/s
        fired = [step for step in range(heard)
                 if binding.fires_on(0.42, now + step / 60.0) is not None]
        self.assertLess(len(fired), heard / 4)

    def test_a_changing_value_is_not_delayed_by_the_keepalive(self):
        """The keepalive is a floor on how often it speaks, never a ceiling."""
        binding = self.binding()
        now = time.monotonic()
        sent = [binding.fires_on(step / 20.0, now + step * 0.05) for step in range(20)]
        self.assertEqual(len([one for one in sent if one is not None]), 20)

    def test_a_trigger_has_no_keepalive_to_give(self):
        """A trigger is an edge, not a held value: there is nothing to restate,
        and restating one would be a second beat."""
        for binding in osc_input.BindingSet.load(
                DESKTOP / "config" / "oscmaps" / "synesthesia.json").bindings:
            if binding.mode == "trigger":
                self.assertEqual(binding.max_interval, 0.0, binding.label)

    def test_it_survives_the_file(self):
        binding = osc_input.Binding(max_interval=0.4)
        again = osc_input.Binding.from_dict(binding.to_dict())
        self.assertEqual(again.max_interval, 0.4)


class TheOscInputSaysWhenItIsNotLanding(unittest.TestCase):
    """The whole failure mode of this wire is silence.

    A binding aimed at an address that never arrives looks exactly like a
    visualiser that is switched off, which looks exactly like a rig running
    normally. Worse, the two directions are independent: a desk whose colour is
    already reaching Synesthesia proves only that the *outbound* half works,
    and that is precisely the evidence that makes the inbound half look like it
    must be fine too.

    So the two states that are otherwise indistinguishable from fine get named
    in the status line, without anyone having to know to open the panel.
    """
    def setUp(self):
        import tempfile
        from eclipse_dmx import viewer as viewer_module
        from eclipse_dmx.viewer import ViewerApp

        self.scratch = tempfile.TemporaryDirectory()
        self.grace = viewer_module.OSC_SILENCE_GRACE
        viewer_module.OSC_SILENCE_GRACE = 0.0

        self.app = ViewerApp(SHOW, pattern="mythos26", midi="", bpm=120.0, osc_in=7803,
                             midimap=Path(self.scratch.name) / "default.json")
        self.said = []
        self.app._say = self.said.append

    def tearDown(self):
        from eclipse_dmx import viewer as viewer_module
        viewer_module.OSC_SILENCE_GRACE = self.grace
        try:
            self.app._quit()
        except Exception:
            pass
        self.scratch.cleanup()

    def osc_lines(self):
        return [line for line in self.said if "osc in:" in line]

    def test_silence_is_named_and_the_other_direction_is_not_the_answer(self):
        self.app._check_osc_is_landing()
        lines = self.osc_lines()
        self.assertTrue(lines, "nothing arriving said nothing")
        self.assertIn("output", lines[0],
                      "it has to name the app's OSC *output*, which is the "
                      "setting distinct from the one already working")

    def test_it_is_said_once_and_not_every_frame(self):
        for _ in range(50):
            self.app._check_osc_is_landing()
        self.assertEqual(len(self.osc_lines()), 1)

    def test_traffic_that_no_binding_takes_is_named_as_that(self):
        """The address-mismatch case, which is the one that bites: the
        uniforms are undocumented and were renamed once already, so a shipped
        map is a set of guesses until something checks it against this build."""
        self.app.osc_traffic.saw_in("/syn/NobodyBoundThis", (0.5,))
        self.app._check_osc_is_landing()

        lines = self.osc_lines()
        self.assertTrue(any("no binding takes" in line for line in lines), lines)

    def test_traffic_a_binding_does_take_says_nothing(self):
        """The working case must stay quiet, or the warning is noise and gets
        ignored on the night it is right."""
        # A real address off the wire, in the shape this build sends - the
        # shipped map has to actually take it for this to mean anything.
        self.app.osc_traffic.saw_in("/audio/hits/bass", (0.5,))
        for _ in range(20):
            self.app._check_osc_is_landing()
        self.assertFalse([line for line in self.osc_lines()
                          if "no binding takes" in line], self.said)


class TheFirewallNote(unittest.TestCase):
    """The half of the diagnosis nobody acts on unless it is specific.

    "A firewall might be holding it" is advice; "ufw is active, its default is
    DROP, and nothing in its rules mentions this port" is a diagnosis. This was
    found by hand on the rig it was written for, after the port had already
    swallowed an evening.
    """
    def note(self, port=7000):
        from eclipse_dmx.cli import _firewall_note
        return _firewall_note(port)

    def test_it_never_guesses(self):
        """Silent whenever it cannot actually tell - another platform, another
        firewall, or rules that need root. A confident wrong answer sends
        someone to reconfigure a firewall that was never the problem."""
        note = self.note()
        if note is None:
            return
        # If it did speak, it read real files and must name what it read.
        self.assertIn("ufw", note)
        self.assertIn("7000", note)

    def test_it_reports_the_port_it_was_asked_about(self):
        note = self.note(9999)
        if note is None:
            self.skipTest("no readable ufw on this machine")
        self.assertIn("9999", note)
        self.assertNotIn("7000", note)

    def test_it_says_nothing_for_a_port_the_rules_mention(self):
        """The rule that lets ssh in is in every ufw install, so it is the one
        port that can be checked for a negative on any machine that has one."""
        import pathlib as _pathlib
        try:
            rules = _pathlib.Path("/etc/ufw/user.rules").read_text(encoding="utf-8")
        except OSError:
            self.skipTest("ufw rules not readable here")
        if "22" not in rules:
            self.skipTest("this ufw does not mention 22 either")
        self.assertIsNone(self.note(22))


class CommandsSurviveTheAudioStream(ShowTest):
    """A cue fired while the bus is streaming must still get its reply.

    `audio` lines come off the OSC listener's thread at a couple of hundred a
    second and are sent with expect_reply=False. Two things in command() are
    what keep that from eating everything else:

      - a streamed line does not touch the reply queue. It used to drain it,
        so any command already waiting had its answer thrown away by traffic
        with nothing to do with it.
      - replying commands are serialised, so a streamed write cannot land
        between another command's drain and its read.

    Found on a live rig, as `no reply to 'layer hit_obelisk mod level off'
    within 5.0s` - from a command that had actually worked.
    """
    def test_a_command_replies_under_a_flood_of_streamed_lines(self):
        import threading

        show = self.running_show(SHOW, midi="", emit_rate=30.0)
        stop = threading.Event()

        def flood():
            # The shape the OSC dispatcher produces: many channels, no reply
            # wanted, from a thread that is not the one issuing commands.
            while not stop.is_set():
                for channel in ("bass", "mid", "high", "bass_hits", "level"):
                    show.command(f"audio {channel} 0.5", expect_reply=False)
                time.sleep(0.002)

        try:
            time.sleep(1.0)
            streamer = threading.Thread(target=flood, daemon=True)
            streamer.start()
            time.sleep(0.3)

            # Twenty in a row: the old failure was a race, so once is not a
            # test of it.
            for _ in range(20):
                self.assertTrue(show.command("channels").startswith("OK"))
        finally:
            stop.set()
            time.sleep(0.1)
            show.stop()

    def test_a_streamed_line_does_not_wait(self):
        """The other half of why it is expect_reply=False: a knob driven at
        frame rate cannot afford a round trip per value."""
        show = self.running_show(SHOW, midi="")
        time.sleep(1.0)
        started = time.monotonic()
        for _ in range(200):
            show.command("audio bass 0.5", expect_reply=False)
        elapsed = time.monotonic() - started
        self.assertLess(elapsed, 1.0, "streamed lines are blocking")


class TheAudioMeter(ShowTest):
    """The bus, as cues: one per channel, for reading it with your eyes.

    Every other look in this rig is a show. This one is an instrument, and it
    exists because the analysis wire has several ways to fail that all present
    as "the lights are not moving" - plus one that is worse and presents as
    nothing at all: a uniform the app publishes and never fills. That is not
    hypothetical; /audio/hits/high is exactly that on the build this was
    written against.
    """

    @staticmethod
    def _protocol(script, frames=ANSWER_FRAMES):
        return run_show(script, frames=frames, config=SHOW)

    # -- the cue list -------------------------------------------------------

    def test_a_cue_per_channel_in_the_buss_own_order(self):
        """Built from the channel table rather than a list written beside it,
        so a channel added to AudioChannel is a cue with nothing else edited.
        Two lists that must agree is one list that eventually does not."""
        lines = self._protocol(["pattern audio", "states"])
        named = [line for line in lines if line.startswith("STATES ")][-1].split()[1:]
        self.assertEqual(tuple(named), AUDIO_STATES)

    def test_the_python_mirror_matches_the_executable(self):
        """A cue per channel, in the channel table's order - and then the beat
        flash, which is not a channel and is parked here while the show's own
        list is empty. See beatPulseState."""
        self.assertEqual(AUDIO_STATES[:len(AUDIO_CHANNELS)], AUDIO_CHANNELS)
        self.assertEqual(AUDIO_STATES[len(AUDIO_CHANNELS):], ("beat_pulse",))

    def test_the_viewers_buttons_match_too(self):
        from eclipse_dmx.viewer import STATE_GROUPS
        groups = dict((name, [label for label, _ in rows]) for name, rows in STATE_GROUPS)
        self.assertEqual(tuple(groups["audio"]), AUDIO_STATES)

    # -- what it draws ------------------------------------------------------

    def test_a_fed_channel_reads_as_its_value(self):
        # Paced, because the look samples the bus in tick(): a script piped in
        # all at once is answered before a single frame has run, and every
        # reading would be the zero it started at.
        lines = TheAudioBus._protocol_paced(
            ["pattern audio", "state bass"] + ["audio bass 1.0"] * 4 + ["params"])
        value = [l for l in lines if l.startswith("PARAM value ")][-1].split()[3]
        self.assertAlmostEqual(float(value), 1.0, places=3)

    def test_an_unfed_channel_is_not_drawn_as_zero(self):
        """The whole point. A channel nobody is filling and a channel sitting
        at zero are the same number and completely different problems, and a
        meter that drew both as black would be useless for the one job it has.

        Checked on the colour rather than the value, because the value *is*
        zero in both cases - it is the amber breath that carries the
        difference, and it is deliberately too dim to be mistaken for a
        reading.
        """
        lines = TheAudioBus._protocol_paced(
            ["pattern audio", "state high_hits", "params"])
        colour = [l for l in lines if l.startswith("PARAM color ")]
        self.assertTrue(colour, lines[-5:])

        # and the look itself reports the channel as unfed
        value = [l for l in lines if l.startswith("PARAM value ")][-1].split()[3]
        self.assertAlmostEqual(float(value), 0.0, places=3)

    def test_the_peak_holds_above_a_value_that_has_dropped(self):
        """A transient is two or three frames wide. Without the peak, the hits
        channels flicker at a rate the eye cannot size, which is the opposite
        of measurable."""
        lines = TheAudioBus._protocol_paced(
            ["pattern audio", "state bass"] + ["audio bass 1.0"] * 4
            + ["audio bass 0.0"] * 2 + ["params"])
        value = float([l for l in lines if l.startswith("PARAM value ")][-1].split()[3])
        peak = float([l for l in lines if l.startswith("PARAM peak ")][-1].split()[3])
        self.assertGreaterEqual(peak, value)

    # -- the surface --------------------------------------------------------

    def test_the_launchpad_has_a_pad_per_cue(self):
        import json
        data = json.loads((DESKTOP / "config" / "midimaps"
                           / "launchpad-all-states.json").read_text())
        pads = [m for m in data["mappings"] if m.get("page") == "audio"]
        self.assertEqual(len(pads), len(AUDIO_STATES))

        named = [a["params"]["name"] for m in pads
                 for a in m["actions"] if a["action"] == "state"]
        self.assertEqual(tuple(named), AUDIO_STATES)

        # every pad also selects the pattern, so a page can be entered from
        # any other machine without a second press
        for pad in pads:
            self.assertIn("audio", [a["params"].get("name") for a in pad["actions"]
                                    if a["action"] == "pattern"])

    def test_the_tab_selects_the_machine_like_every_other_tab(self):
        """One press from anywhere, the way the scanner and jacket tabs work.

        It used to also switch three additive hit layers off, because the show
        carried them permanently and they rode over the meter. The show has no
        layers now - see test_the_show_declares_no_layers_at_all - so there is
        nothing to switch and the tab is a tab again.
        """
        import json
        data = json.loads((DESKTOP / "config" / "midimaps"
                           / "launchpad-all-states.json").read_text())
        tab = next(m for m in data["mappings"]
                   if any(a["action"] == "page" and a["params"]["name"] == "audio"
                          for a in m["actions"]))
        self.assertIn("audio", [a["params"].get("name") for a in tab["actions"]
                                if a["action"] == "pattern"])

    def test_no_tab_wires_a_layer_that_does_not_exist(self):
        """The tabs used to send `layer hit_ring mod level ...`. With the
        layers gone those are ERRs on every press - and an ERR on a tab is a
        refusal the operator sees in the middle of a set."""
        import json
        data = json.loads((DESKTOP / "config" / "midimaps"
                           / "launchpad-all-states.json").read_text())
        declared = set()
        for path in (SHOW, LAYERS):
            declared.update(layer["name"] for layer in Config.load(path).layers)

        for mapping in data["mappings"]:
            for action in mapping["actions"]:
                line = action["params"].get("line", "") if action["action"] == "command" else ""
                if line.startswith("layer "):
                    self.assertIn(line.split()[1], declared, mapping["label"])


class TheOscInputHints(unittest.TestCase):
    """What to type at the far end, said when the port opens.

    Synesthesia's output address field takes a numeric IP and nothing else -
    no hostname, no .local - so the number has to come from somewhere, and
    every other source for it is a guess made under pressure. Its own default
    is 127.0.0.1, which on a rig where the visualiser sits on a different
    machine means "into my own loopback" and is completely silent about it.
    """
    def hints(self, port=7000):
        from eclipse_dmx.cli import osc_input_hints
        return osc_input_hints(port)

    def test_it_names_an_address_that_is_not_loopback(self):
        lines = [line for line in self.hints() if "output" in line]
        if not lines:
            self.skipTest("this machine has no non-loopback address")
        self.assertNotIn("127.0.0.1:", lines[0])
        self.assertIn(":7000", lines[0])

    def test_it_says_localhost_means_the_other_machine(self):
        """The trap itself, named. It is the one setting that is *right* when
        both halves are tested on one machine and silently wrong the moment
        they are not, which is why it survives a test setup and fails a rig."""
        lines = [line for line in self.hints() if "output" in line]
        if not lines:
            self.skipTest("this machine has no non-loopback address")
        self.assertIn("127.0.0.1", lines[0])

    def test_every_hint_is_one_pasteable_line(self):
        """These go to a status line as well as a terminal, so a newline or a
        shell continuation surviving into one is a command that will not
        paste."""
        for line in self.hints():
            self.assertNotIn("\n", line)
            self.assertNotIn("\\", line)


class TheOscPanel(unittest.TestCase):
    """What is arriving, what is going out, and what can honestly be claimed."""

    def traffic(self):
        from eclipse_dmx.osc_panel import OscTraffic
        return OscTraffic()

    def test_addresses_are_discovered_rather_than_listed(self):
        """Nothing here knows what addresses exist. A build of the app that
        nests its uniforms differently, or a scene publishing controls of its
        own, shows up without this code being edited - which is the whole
        point, given the addresses are undocumented."""
        traffic = self.traffic()
        traffic.saw_in("/syn/NobodyWroteThisDown", (0.5,))
        traffic.saw_out("/controls/global/color/1", (1.0, 0.0, 0.0), True)

        state = traffic.snapshot()
        self.assertEqual([row[0] for row in state["in"]], ["/syn/NobodyWroteThisDown"])
        self.assertEqual([row[0] for row in state["out"]], ["/controls/global/color/1"])

    def test_rows_stay_in_the_order_they_were_discovered(self):
        """Sorting by rate or value would make the list jump under the cursor
        exactly while it is being read."""
        traffic = self.traffic()
        for address in ("/c", "/a", "/b"):
            traffic.saw_in(address, (0.1,))
        for _ in range(50):
            traffic.saw_in("/b", (0.9,))
        self.assertEqual([row[0] for row in traffic.snapshot()["in"]], ["/c", "/a", "/b"])

    def test_a_scene_announcement_is_recorded_as_the_app_speaking(self):
        """The one message the visualiser sends about *itself*, so it is the
        closest thing to a handshake this link has."""
        traffic = self.traffic()
        traffic.saw_in("/scenes/neongrid", ())
        self.assertEqual(traffic.snapshot()["scene"], "/scenes/neongrid")

    def test_a_refused_send_is_counted_apart_from_a_taken_one(self):
        traffic = self.traffic()
        traffic.saw_out("/a", (1.0,), True)
        traffic.saw_out("/a", (1.0,), False)
        state = traffic.snapshot()
        self.assertEqual((state["sends"], state["send_failures"]), (2, 1))

    def test_the_table_is_bounded(self):
        """Something misconfigured into spraying unique addresses must not be
        able to grow this for the length of a set."""
        traffic = self.traffic()
        for index in range(traffic.max_rows + 20):
            traffic.saw_in(f"/syn/{index}", (0.0,))
        state = traffic.snapshot()
        self.assertEqual(len(state["in"]), traffic.max_rows)
        self.assertEqual(state["overflowed"], 20)

    def test_the_link_tells_it_what_went_out(self):
        """SynesthesiaLink.on_send, which is why _send takes an address rather
        than a finished packet: one place that knows *what* is being sent as
        well as that something is."""
        from eclipse_dmx.osc import SynesthesiaLink
        seen = []
        link = SynesthesiaLink("127.0.0.1:9", on_send=lambda a, v, ok: seen.append((a, v)))
        try:
            link.send_color((255, 0, 0))
            link.send_scene("NeonGrid")
            link.send_favslot(3)
        finally:
            link.close()

        self.assertEqual([address for address, _ in seen],
                         ["/controls/global/color/1", "/scenes/neongrid", "/favslots/3"])
        self.assertEqual(seen[0][1], (1.0, 0.0, 0.0))

    def test_an_observer_that_throws_cannot_stop_a_send(self):
        """It is a diagnostic hanging off the side of a link that is itself
        decoration hanging off the side of a show."""
        from eclipse_dmx.osc import SynesthesiaLink

        def explode(*_args):
            raise RuntimeError("no")

        link = SynesthesiaLink("127.0.0.1:9", on_send=explode)
        try:
            link.send_color((255, 0, 0))      # must not raise
            self.assertEqual(link.sent, 1)
        finally:
            link.close()


class SharedBeatTriggers(ShowTest):
    """When a hit lands, decided once for the whole rig.

    Every beat look used to work this out privately - read the clock, watch the
    beat number, check the rate, fire. Four looks meant four answers, and they
    came apart in three ways that all read on a rig as the UV not being with the
    show: a look that is not showing is not counting, so it entered cold; entry
    always fired a hit, at whatever fraction of a beat the operator pressed the
    button; and a cross-fade ticks both looks, so two envelopes ran at once.

    The decision moved to edmx::TriggerRack, is made once a frame before
    anything ticks, and is shared. What is tested here is the part that was
    actually broken: a look entered off the beat joins the grid instead of
    starting one of its own.
    """
    def _uv_hits(self, enter_offset, bpm=120.0, settle=2.0):
        """UV hit instants, and the moment `flash` was asked for.

        The offset puts the entry deliberately off the beat, which is the whole
        point: at 120bpm a beat is 500ms and 0.13 into one is nowhere near it.
        """
        frames = []
        show = self.running_show(STAGE, midi="", bpm=bpm,
                                 on_frame=lambda f: frames.append((time.time(), f)),
                                 emit_rate=40.0)
        show.set_state("scan_idle")
        time.sleep(1.0)
        time.sleep(enter_offset)
        entered = time.time()
        show.layers["uv"].set_state("flash")
        time.sleep(settle)

        uv = show.layers["uv"].fixtures[0]
        levels = [(t, f[uv][0]) for t, f in frames if len(f) > uv]
        hits = [t for (t, v), (_pt, pv) in zip(levels[1:], levels) if v > pv + 40]
        return entered, [h for h in hits if h >= entered]

    def test_the_uv_hits_on_the_beat(self):
        _entered, hits = self._uv_hits(0.0, settle=3.0)
        self.assertGreaterEqual(len(hits), 4, "the UV did not flash")

        # 500ms apart at 120bpm. Loose enough for a 25ms frame to land either
        # side of the beat, tight enough that a look on its own count drifts
        # out of it within a couple of bars.
        for first, second in zip(hits, hits[1:]):
            self.assertAlmostEqual(second - first, 0.5, delta=0.06)

    def test_entering_off_the_beat_waits_for_the_grid(self):
        """The bug this exists for.

        `layer uv state flash` used to fire the envelope on the look's first
        tick - which is the instant the button was pressed, and is off the beat
        by construction. So the UV cracked at a time of the operator's choosing
        and the truss carried on at the music's, and the two never agreed until
        the next cue change moved it somewhere else.

        Now the entry is not a hit: the look joins the shared trigger, and the
        first flash lands when the rig's next beat does.
        """
        for offset in (0.13, 0.27, 0.41):
            entered, hits = self._uv_hits(offset)
            self.assertGreaterEqual(len(hits), 3, f"no flashes after entering at +{offset}")

            # It waited: the first hit is not at the moment of entry.
            waited = hits[0] - entered
            self.assertGreater(waited, 0.03,
                               f"flashed on entry at +{offset}, off the beat")

            # And what it waited for was the grid - the beats after it are
            # 500ms apart, and the first one is on the same grid, not offset
            # from it by however late the button was.
            span = hits[-1] - hits[0]
            off_grid = min(span % 0.5, 0.5 - (span % 0.5))
            self.assertLess(off_grid, 0.05,
                            f"first flash off the grid by {off_grid:.3f}s at +{offset}")

    def test_a_cue_still_comes_up_lit(self):
        """The other half of the trade, and the reason entry ever fired a hit.

        A show cue must not open dark for up to a bar - that reads as a cue
        that did not come up. So `entry_hit` stays on for the show's looks and
        off for the UV, and the two behaviours are one knob rather than one
        hardcoded compromise.
        """
        for offset in (0.13, 0.31):
            frames = []
            show = ShowController(SHOW, dry_run=True, pattern="audio", midi="", bpm=120.0,
                                  on_frame=lambda f: frames.append((time.time(), f)),
                                  emit_rate=40.0)
            try:
                # From a cue that is not the flash, so what is measured is the
                # entry and not whatever was already on the rig. `level` with
                # nothing feeding it is the meter's dim amber breath - lit, but
                # nowhere near a hit, and too slow to read as a rising edge.
                show.set_state("level")
                time.sleep(1.0 + offset)
                entered = time.time()
                show.set_state("beat_pulse")
                time.sleep(1.0)
            finally:
                show.stop()

            after = [(t, max(f[0])) for t, f in frames if t >= entered]
            rising = next((t - entered for (t, v), (_pt, pv) in zip(after[1:], after)
                           if v > pv + 5), None)
            self.assertIsNotNone(rising, f"beat_pulse never lit at +{offset}")

            # Sooner than the next beat, which is what "did not wait" means.
            # It is not instant: a command crosses stdin and the cue cross-fades
            # in, so this is bounded rather than asserted at zero.
            self.assertLess(rising, 0.5 - offset,
                            f"cue waited for the grid at +{offset}")

    def test_entry_hit_is_a_knob_and_the_uv_opens_with_it_off(self):
        show = self.running_show(STAGE, midi="", bpm=120.0, emit_rate=20.0)
        time.sleep(0.8)
        uv = show.layers["uv"]
        uv.set_state("flash")
        time.sleep(0.5)

        entry = uv.get_param("entry_hit")
        self.assertIsNotNone(entry, "the UV flash has no entry_hit knob")
        self.assertTrue(entry.is_bool)
        self.assertFalse(entry.value, "the UV should join the grid, not start one")


        # and a cue on a list is the other way round - a different config,
        # because beat_pulse is a cue the rig comes up on and the UV is the
        # scanner stage's layer
        show = self.running_show(SHOW, pattern="audio", midi="", bpm=120.0, emit_rate=20.0)
        show.set_state("beat_pulse")
        time.sleep(0.8)
        self.assertTrue(show.get_param("entry_hit").value)


class TheStageGeometry(unittest.TestCase):
    """The ring's circle is written in three places and read from one.

    The looks read kRing* in scanner_patterns.h; the desk draws
    devices/scanner_ring.json; the live game writes the same circle inline in
    afterglow's eclipse_engine.py. Nothing checks them against each other at
    runtime, so this does.
    """
    HEADER = DESKTOP.parent / "src" / "relics" / "scanner" / "scanner_patterns.h"

    def _constant(self, name: str) -> float:
        """A `constexpr float NAME = <number>f` out of the header.

        kRingCenterY is spelled as a sum of two others rather than a number,
        so it is resolved here the same way.
        """
        import re

        text = self.HEADER.read_text(encoding="utf-8")
        match = re.search(rf"constexpr float {name} = (-?[\d.]+)f", text)
        if match is None and name == "kRingCenterY":
            return self._constant("kStageOriginY") + self._constant("kRingRadius")
        self.assertIsNotNone(match, f"{name} not found in {self.HEADER}")
        return float(match.group(1))

    def test_the_ring_device_is_the_looks_circle(self):
        cx, cy, r = (self._constant(n) for n in ("kRingCenterX", "kRingCenterY", "kRingRadius"))
        ring = Config.load(DESKTOP / "devices" / "scanner_ring.json").devices[0]
        self.assertEqual(len(ring.fixtures), 35)
        for fixture in ring.fixtures:
            distance = ((fixture.position[0] - cx) ** 2 + (fixture.position[1] - cy) ** 2) ** 0.5
            self.assertAlmostEqual(distance, r, places=3, msg=fixture.name)
        # pixel 0 at the top of the circle, the way the physical ring runs
        self.assertAlmostEqual(ring.fixtures[0].position[1], cy + r, places=3)

    def test_the_ring_says_what_it_is(self):
        """A look that treats the ring as the ring finds it by its space."""
        ring = Config.load(DESKTOP / "devices" / "scanner_ring.json").devices[0]
        self.assertEqual(ring.space, "ring")
        obelisk = Config.load(DESKTOP / "devices" / "obelisk.json").devices[0]
        self.assertEqual(obelisk.space, "obelisk")
        pars = Config.load(DESKTOP / "devices" / "uking_par36_x10.json").devices[0]
        self.assertEqual(pars.space, "truss")

    def test_the_rings_lowest_pixel_is_on_the_origin_line(self):
        """The ring's bottom, the truss and the beat's origin are one line."""
        origin = self._constant("kStageOriginY")
        ring = Config.load(DESKTOP / "devices" / "scanner_ring.json").devices[0]
        lowest = min(fixture.position[1] for fixture in ring.fixtures)
        # 35 pixels round a circle put none exactly at the bottom; the
        # nearest is a hundredth above it
        self.assertAlmostEqual(lowest, origin, delta=0.05)
        pars = Config.load(DESKTOP / "config" / "scanner_stage.json").devices[2]
        self.assertAlmostEqual(pars.placement.offset[1], origin)
        self.assertEqual(self._constant("kStageBottom"), 0.0)

    def test_the_game_writes_the_same_circle(self):
        # desktop/ -> eclipse-os/ -> afterglow/, when this is the submodule
        engine = DESKTOP.parents[1] / "main-py" / "lib" / "jr_lib" / "eclipse_engine.py"
        if not engine.exists():
            raise unittest.SkipTest("not inside the afterglow checkout")
        import re

        cx, cy = self._constant("kRingCenterX"), self._constant("kRingCenterY")
        text = engine.read_text(encoding="utf-8")
        centre_x = re.search(r"round\((-?[\d.]+) \+ 3\.5 \* math\.sin\(angle\), 4\)", text)
        centre_y = re.search(r"round\((-?[\d.]+) \+ 3\.5 \* math\.cos\(angle\), 4\)", text)
        self.assertIsNotNone(centre_x)
        self.assertIsNotNone(centre_y)
        self.assertAlmostEqual(float(centre_x.group(1)), cx)
        self.assertAlmostEqual(float(centre_y.group(1)), cy)


class TheLinkProtocol(ShowTest):
    """The wire format and the relic's end of it, exercised by the executable.

    `--link-selftest` drives elink and a real ObeliskCore with no hardware:
    resync, checksums, the takeover and the handback. It is the firmware's own
    code, so this is the closest thing to testing the sculpture that exists
    without flashing one.
    """
    def test_selftest_passes(self):
        executable = self.executable
        result = subprocess.run(
            [str(executable), "--link-selftest"],
            capture_output=True, text=True, timeout=120,
        )

        failures = [line for line in result.stdout.splitlines() if "FAIL" in line]
        self.assertEqual(failures, [], "\n".join(failures))
        self.assertIn("SELFTEST PASS", result.stdout)
        self.assertEqual(result.returncode, 0)

    def test_selftest_covers_the_obelisks_own_frame(self):
        """A 344-pixel frame is the thing that will actually be sent."""
        executable = self.executable
        result = subprocess.run(
            [str(executable), "--link-selftest"],
            capture_output=True, text=True, timeout=120,
        )

        checks = [line for line in result.stdout.splitlines() if line.startswith("SELFTEST ok")]
        self.assertTrue(any("obelisk: all 1032 bytes correct" in line for line in checks))
        self.assertTrue(any("firmware: desk took the pixels" in line for line in checks))
        self.assertTrue(any("firmware: pixels handed back" in line for line in checks))

    def test_link_commands_are_refused_off_a_link(self):
        """A DMX show has nothing to hand over, and should say so rather than
        pretend."""
        show = self.running_show(RIG)
        time.sleep(0.4)
        with self.assertRaises(ShowError):
            show.set_link_mode("cue")
        # And the show survives being asked.
        self.assertTrue(show.is_running)

    def test_a_bad_link_mode_is_caught_before_the_wire(self):
        show = self.running_show(RIG)
        with self.assertRaises(ShowError):
            show.set_link_mode("sideways")

    def test_probe_does_not_claim_a_dmx_widget(self):
        """The whole point of probing rather than guessing at port names.

        Passes trivially with nothing attached; the case it guards is a machine
        with a widget on it, which is this one.
        """
        executable = self.executable
        result = subprocess.run(
            [str(executable), "--probe-relics"],
            capture_output=True, text=True, timeout=120,
        )

        for line in result.stdout.splitlines():
            if line.startswith("RELIC "):
                # Anything listed answered a Hello by name, so it really is one.
                self.assertIn("\t", line)


class PositionStep(unittest.TestCase):
    """One entry describing a run rather than a point."""

    def test_a_bank_ramps_from_its_stated_position(self):
        config = Config.from_dict({
            "fixtures": [
                {"profile": "rgb3", "name": "run", "address": 1, "count": 4,
                 "position": [2.0, 10.0], "position_step": [0.0, -1.0]}
            ],
        })
        self.assertEqual(
            [f.position for f in config.fixtures],
            [[2.0, 10.0], [2.0, 9.0], [2.0, 8.0], [2.0, 7.0]],
        )

    def test_without_a_step_the_whole_bank_stays_put(self):
        """The behaviour that was there before, unchanged."""
        config = Config.from_dict({
            "fixtures": [
                {"profile": "rgb3", "name": "run", "address": 1, "count": 3,
                 "position": [0.5, 0.25]}
            ],
        })
        self.assertEqual([f.position for f in config.fixtures], [[0.5, 0.25]] * 3)

    def test_without_a_position_a_bank_still_spreads_evenly(self):
        config = Config.from_dict({
            "fixtures": [{"profile": "rgb3", "name": "run", "address": 1, "count": 3}],
        })
        self.assertEqual([f.position for f in config.fixtures],
                         [[0.0, 0.0], [0.5, 0.0], [1.0, 0.0]])


class Layout(unittest.TestCase):
    """The viewer's picture is the config's shape, not a hardcoded rig."""

    def setUp(self):
        from eclipse_dmx.viewer import plan_layout
        self.plan = plan_layout

    def test_rig_spreads_across_the_frame(self):
        places = self.plan(Config.load(RIG))
        self.assertEqual(len(places), 10)
        self.assertEqual(places[0].name, "par_1")
        self.assertEqual(places[0].x, 0.0)
        self.assertEqual(places[-1].x, 1.0)

    def test_addresses_are_shown_in_the_configs_numbering(self):
        places = self.plan(Config.load(RIG))
        self.assertEqual([p.address for p in places], [1, 8, 15, 22, 29, 36, 43, 50, 57, 64])

    def test_a_flat_rig_is_centred_vertically(self):
        places = self.plan(Config.load(RIG))
        self.assertTrue(all(p.y == 0.5 for p in places))

    def test_single_fixture_is_centred(self):
        config = Config()
        config.add_fixture(Fixture(name="only", start_channel=1))
        self.assertEqual(self.plan(config)[0].x, 0.5)

    def test_missing_positions_fall_back_to_patch_order(self):
        config = Config()
        for index in range(4):
            config.add_fixture(Fixture(name=f"f{index}", start_channel=1 + index * 3))
        self.assertEqual(
            [round(p.x, 3) for p in self.plan(config)], [0.0, 0.333, 0.667, 1.0]
        )

    def test_positions_are_normalised_not_taken_literally(self):
        """So a rig can be measured in metres, or anything else."""
        config = Config()
        for index, metres in enumerate([2.0, 4.0, 6.0]):
            config.add_fixture(
                Fixture(name=f"f{index}", start_channel=1 + index * 3, position=[metres, 0.0])
            )
        self.assertEqual([p.x for p in self.plan(config)], [0.0, 0.5, 1.0])


class PatternRegistry(unittest.TestCase):
    def test_relic_patterns_are_registered(self):
        executable_or_skip()
        from eclipse_dmx.patterns import list_patterns

        names = list_patterns()
        for expected in ("obelisk_seasons", "obelisk_theater", "obelisk_mono"):
            self.assertIn(expected, names)

    def test_static_list_matches_the_executable(self):
        """config.PATTERN_NAMES is duplicated from pattern.cpp; catch the drift."""
        executable_or_skip()
        from eclipse_dmx.config import PATTERN_NAMES
        from eclipse_dmx.patterns import list_patterns

        self.assertEqual(sorted(list_patterns()), sorted(PATTERN_NAMES))


class FrameStream(ShowTest):
    """--emit-frames, end to end, with no hardware."""

    def test_frames_arrive_and_move(self):
        frames = []
        show = self.running_show(RIG, on_frame=frames.append, emit_rate=30.0)
        show.set_pattern("obelisk_seasons")
        time.sleep(1.5)

        self.assertGreater(len(frames), 20)
        self.assertTrue(all(len(frame) == 10 for frame in frames))
        self.assertGreater(len({tuple(frame) for frame in frames}), 5)

    def test_fixture_names_are_reported(self):
        show = self.running_show(RIG)
        time.sleep(0.5)
        self.assertEqual(show.fixture_names, [f"par_{i}" for i in range(1, 11)])

    def test_frames_do_not_pile_up_in_events(self):
        """A show left running for an hour must not grow a list per frame."""
        frames = []
        show = self.running_show(RIG, on_frame=frames.append)
        time.sleep(1.2)

        self.assertGreater(len(frames), 20)
        self.assertLess(len(show.events), 20)

    def test_blackout_reaches_the_wire(self):
        frames = []
        show = self.running_show(RIG, on_frame=frames.append)
        show.set_pattern("obelisk_mono")
        time.sleep(0.5)
        show.blackout(True)
        time.sleep(0.6)
        self.assertTrue(all(colour == (0, 0, 0) for colour in frames[-1]))

        show.blackout(False)
        time.sleep(0.6)
        self.assertTrue(any(colour != (0, 0, 0) for colour in frames[-1]))

    def test_master_dims_the_wire(self):
        frames = []
        show = self.running_show(RIG, on_frame=frames.append)
        show.set_pattern("obelisk_mono")
        time.sleep(0.5)
        full = frames[-1][0]
        show.set_master(0.25)
        time.sleep(0.5)
        dim = frames[-1][0]

        self.assertLess(sum(dim), sum(full))

    def test_emit_rate_is_honoured(self):
        """Asking for 20 must give 20, not whatever the render loop rounds to."""
        stamps = []
        show = self.running_show(
            RIG, on_frame=lambda f: stamps.append(time.monotonic()), emit_rate=20.0)
        time.sleep(0.4)
        stamps.clear()
        start = time.monotonic()
        time.sleep(2.0)
        measured = len(stamps) / (time.monotonic() - start)

        self.assertAlmostEqual(measured, 20.0, delta=20.0 * 0.15)


class ObeliskOnTheDesk(ShowTest):
    """The sculpture's own looks, rendered on the sculpture's own shape."""

    def _last_frame(self, pattern):
        """One settled frame of `pattern`, as 344 (r, g, b)."""
        frames = []
        show = self.running_show(OBELISK, on_frame=frames.append, emit_rate=20.0)
        show.set_pattern(pattern)
        time.sleep(1.2)

        self.assertTrue(frames, "no frames arrived")
        return frames[-1]

    def test_preview_runs_with_no_widget_attached(self):
        """Not --dry-run: the config's own output, on a machine with no wire.

        "auto" with nothing plugged in is a startup error for a DMX rig, and
        must not be one for a rig that was never going to touch a wire.
        """
        frames = []
        show = self.running_show(OBELISK, dry_run=False, on_frame=frames.append)
        time.sleep(1.0)
        self.assertTrue(show.is_running)

        self.assertTrue(frames)

    def test_the_whole_strip_reaches_the_frame(self):
        """Including the 173 pixels that sit past channel 512."""
        frame = self._last_frame("obelisk_seasons")
        self.assertEqual(len(frame), OBELISK_PIXELS)
        self.assertTrue(any(colour != (0, 0, 0) for colour in frame[171:]))

    def test_the_patch_matches_what_python_resolved(self):
        """Both sides parse this file, so they can disagree. Catch it."""
        result = subprocess.run(
            [str(self.executable), "--config", str(OBELISK), "--show-patch"],
            capture_output=True, text=True, timeout=60,
        )
        stated = [line.split() for line in result.stdout.splitlines() if line.startswith("PATCH ")]
        self.assertEqual(len(stated), OBELISK_PIXELS)

        config = Config.load(OBELISK)
        for fields, fixture in zip(stated, config.fixtures):
            name, red = fields[1], int(fields[2].removeprefix("r="))
            self.assertEqual(name, fixture.name)
            self.assertEqual(red, fixture.start_channel + fixture.offsets()[0])

    def test_each_side_gets_its_own_season(self):
        """The reason the coordinates have to be literal.

        obelisk_seasons picks a palette from a node's x, which is which strip it
        is on. Collapse the rig to a single 0..1 position and every side gets
        the same colour - which looks fine, and is the wrong picture entirely.
        """
        frame = self._last_frame("obelisk_seasons")
        sides = [frame[side * 2 * OBELISK_SIDE_LENGTH + 20] for side in range(4)]
        self.assertEqual(len(set(sides)), 4, f"sides are not distinct: {sides}")

    def test_a_strip_is_not_flat(self):
        """And the reason y has to span 43 rather than 0..1: the noise field."""
        frame = self._last_frame("obelisk_seasons")
        run = frame[:OBELISK_SIDE_LENGTH]
        self.assertGreater(len(set(run)), 1)


class JacketStateMachine(ShowTest):
    """The jacket's looks, driven over the protocol instead of by buttons."""

    def test_states_are_announced(self):
        show = self.running_show(RIG)
        show.command("pattern jacket")
        time.sleep(0.5)
        self.assertIn("digital_void", show.state_names)
        self.assertEqual(len(show.state_names), 12)

    def test_config_opens_on_its_chosen_state(self):
        """The rig config names a state; digital_void is nearly black."""
        show = self.running_show(RIG)
        time.sleep(0.5)
        self.assertEqual(show.current_state, Config.load(RIG).pattern.state)

    def test_switching_state_changes_the_rig(self):
        frames = []
        show = self.running_show(RIG, on_frame=frames.append, emit_rate=20.0)
        show.command("pattern jacket")
        seen = []
        for state in ("parrot", "blue_magic", "campfire"):
            show.set_state(state)
            time.sleep(1.0)          # past the 0.4s cross-fade
            seen.append(tuple(frames[-1]))
        self.assertEqual(len(set(seen)), 3)

    def test_unknown_state_is_rejected_without_dying(self):
        show = self.running_show(RIG)
        show.command("pattern jacket")
        time.sleep(0.4)
        with self.assertRaises(ShowError):
            show.set_state("not_a_look")
        self.assertTrue(show.is_running)

    def test_state_needs_a_state_machine(self):
        show = self.running_show(RIG)
        show.command("pattern obelisk_seasons")
        time.sleep(0.4)
        self.assertEqual(show.state_names, [])
        with self.assertRaises(ShowError):
            show.set_state("campfire")

    def test_input_modulates_the_look(self):
        frames = []
        show = self.running_show(RIG, on_frame=frames.append, emit_rate=20.0)
        show.command("pattern jacket")
        show.set_state("digital_void")
        time.sleep(1.0)
        before = tuple(frames[-1])
        show.set_input("a", True)
        time.sleep(1.2)
        after = tuple(frames[-1])
        self.assertNotEqual(before, after)


class MidiSettings(unittest.TestCase):
    """The config side of the beat clock. No device needed."""

    def test_the_show_config_loads_clean(self):
        config = Config.load(SHOW)
        self.assertEqual(config.validate(), [])
        # The show config currently opens on the audio meter rather than on
        # the show - deliberately, while the Synesthesia wire is being trusted.
        # See the note by "pattern" in config/mythos26.json.
        self.assertEqual(config.pattern.name, "audio")
        self.assertEqual(config.pattern.state, "level")
        self.assertTrue(config.midi.enabled)

    def test_naming_a_port_enables_midi(self):
        """Writing `"midi": {"port": ...}` and getting silence would be a trap."""
        config = Config.from_dict(
            {"midi": {"port": "loopMIDI"}, "fixtures": [{"name": "a", "start_channel": 1}]}
        )
        self.assertTrue(config.midi.enabled)

    def test_absent_midi_block_means_no_midi(self):
        config = Config.from_dict({"fixtures": [{"name": "a", "start_channel": 1}]})
        self.assertFalse(config.midi.enabled)
        self.assertEqual(config.midi.bpm, 128.0)

    def test_round_trips(self):
        config = Config.load(SHOW)
        again = Config.from_dict(config.to_dict())
        self.assertEqual(again.midi, config.midi)

    def test_mixxx_note_numbers_are_the_defaults(self):
        """Its beat is note 50 and its tempo note 52; see edmx/midi_input.h."""
        midi = MidiConfig()
        self.assertEqual((midi.beat_note, midi.bpm_note), (50, 52))

    def test_all_three_loudness_signals_are_read(self):
        """64 instantaneous, 68 two-second average, 69 a meter bar."""
        midi = MidiConfig()
        self.assertEqual(
            (midi.vu_instant_note, midi.vu_average_note, midi.vu_meter_note),
            (64, 68, 69),
        )

    def test_two_signals_cannot_share_a_note(self):
        with self.assertRaises(ConfigError):
            MidiConfig(vu_instant_note=68, vu_average_note=68).validate()

    def test_a_signal_can_be_switched_off(self):
        MidiConfig(vu_instant_note=-1, vu_meter_note=-1).validate()

    def test_a_meter_cannot_share_the_beat_note(self):
        with self.assertRaises(ConfigError):
            MidiConfig(beat_note=64, vu_instant_note=64).validate()

    def test_a_tempo_that_is_not_a_tempo_is_rejected(self):
        with self.assertRaises(ConfigError):
            MidiConfig(bpm=5.0).validate()

    def test_beat_and_bpm_on_one_note_is_rejected(self):
        with self.assertRaises(ConfigError):
            MidiConfig(beat_note=50, bpm_note=50).validate()

    def test_no_carrier_is_rejected(self):
        with self.assertRaises(ConfigError):
            MidiConfig(enabled=True, clock=False, notes=False).validate()

    def test_bad_channel_is_rejected(self):
        with self.assertRaises(ConfigError):
            MidiConfig(beat_channel=17).validate()

    def test_unknown_mythos_state_is_rejected(self):
        config = Config.load(SHOW)
        config.pattern.state = "slot_13"     # twelve of them, and no more
        with self.assertRaises(ConfigError):
            config.validate()


class Mythos26(ShowTest):
    """The show, driven over the protocol."""

    def _show(self, **kwargs):
        # --midi "" so the test never opens a device that happens to be
        # plugged into the machine running it.
        #
        # `pattern` named explicitly because the show config currently opens on
        # the audio meter rather than on the show - see the note by "pattern"
        # in config/mythos26.json. These tests are about the show's own cue
        # list, so they ask for it rather than inheriting whatever is default
        # this week.
        kwargs.setdefault("midi", "")
        show = self.running_show(SHOW, **kwargs)
        show.set_pattern("mythos26")
        return show

    def _beat_show(self, **kwargs):
        """The beat look, wherever it is living.

        The show's own cue list is twelve empty slots for now, so the flash
        these tests are about is a cue on the audio bus rather than the show's
        opening state - see beatPulseState. What is being tested is the look
        and the clock under it, neither of which moved.
        """
        show = self._show(**kwargs)
        show.set_pattern("audio")
        show.set_state("beat_pulse")
        return show

    def test_twelve_states_in_table_order(self):
        show = self._show(on_frame=lambda f: None)
        time.sleep(0.5)
        self.assertEqual(show.state_names, list(MYTHOS26_STATES))

    def test_the_static_list_matches_the_executable(self):
        show = self._show(on_frame=lambda f: None)
        time.sleep(0.5)
        self.assertEqual(tuple(show.state_names), MYTHOS26_STATES)

    def test_it_pulses_white_on_the_beat(self):
        """Full white once a beat, well down between, and every fixture together."""
        frames = []
        show = self._beat_show(on_frame=frames.append, bpm=120.0, emit_rate=40.0)
        time.sleep(2.5)  # five beats at 120

        self.assertTrue(frames, "no frames arrived")

        # White: a hit has r == g == b. A colour cast here would mean the
        # channel order is wrong, which is the one thing white catches.
        peaks = [f for f in frames if max(f[0]) > 200]
        self.assertTrue(peaks, "the rig never reached full brightness")
        for frame in peaks:
            for red, green, blue in frame:
                self.assertEqual((red, green, blue), (red, red, red))

        # The whole rig hits together. Checked on the full-white frames: a
        # device renders on its own gamma when its file says so (the obelisk,
        # at NeoPixel's 2.6, sits in this rig beside pars at the master's
        # 2.2), and only at full white do every curve's bytes agree. A hit
        # is full white by the look's design, so a beat with no such frame
        # would be a beat the rig missed.
        full = [f for f in peaks if max(f[0]) == 255]
        self.assertTrue(full, "no frame reached full white")
        for frame in full:
            self.assertEqual(len(set(frame)), 1)

        # And it comes back down in between, rather than sitting lit.
        #
        # Not "reaches zero" any more: the envelope is 1.2s against a 500ms beat
        # and retriggers by holding rather than by cutting to black, so the
        # trough is a third of full and not dark. Asserting zero here passed
        # only on the cross-fade frames at the top of the show, which is not
        # what it meant to be checking. See RetriggerMode::RestartHold.
        trough = min(max(f[0]) for f in frames[len(frames) // 2:])
        self.assertLess(trough, 128, "the rig never came back down between hits")

    def test_the_tempo_sets_how_often(self):
        """The rig pulses once per beat, at whatever the tempo is."""
        seconds = 4.0

        def count_pulses(bpm):
            frames = []
            show = self._beat_show(on_frame=frames.append, bpm=bpm, emit_rate=40.0)
            try:
                time.sleep(seconds)
            finally:
                show.stop()

            # Count rising edges rather than bright frames: a pulse spans
            # several frames, and how many depends on the frame rate we got.
            lit = [max(f[0]) > 128 for f in frames]
            return sum(1 for i in range(1, len(lit)) if lit[i] and not lit[i - 1])

        # Checked against the tempo rather than against each other. A ratio is
        # the weaker claim and, at these counts, lands on the boundary: one
        # pulse either side of the window swings it either way.
        for bpm in (60.0, 120.0):
            with self.subTest(bpm=bpm):
                expected = bpm / 60.0 * seconds
                counted = count_pulses(bpm)
                self.assertAlmostEqual(counted, expected, delta=2.0)

    def test_bpm_is_reported_back(self):
        show = self._show(on_frame=lambda f: None, bpm=90.0)
        time.sleep(1.5)
        self.assertAlmostEqual(show.bpm, 90.0, delta=1.0)
        self.assertFalse(show.beat_locked, "nothing external is driving it")
        self.assertIn("bpm=90", show.status())

    def test_tapping_a_beat_relights_the_rig(self):
        frames = []
        show = self._beat_show(on_frame=frames.append, bpm=40.0, emit_rate=40.0)
        time.sleep(1.2)  # well past the 200ms fall, and before the next beat
        self.assertEqual(max(frames[-1][0]), 0)

        # Take the peak across the frames that follow rather than sampling
        # one a fixed wait later: the pulse is 200ms wide, a frame is 25ms,
        # and a loaded machine will happily put those two out of step.
        mark = len(frames)
        show.tap_beat()

        peak = 0
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline and peak <= 128:
            for frame in frames[mark:]:
                peak = max(peak, max(frame[0]))
            time.sleep(0.02)

        self.assertGreater(peak, 128)

    def test_a_bad_tempo_is_rejected_without_dying(self):
        show = self._show(on_frame=lambda f: None)
        with self.assertRaises(ShowError):
            show.set_bpm(5.0)
        self.assertTrue(show.is_running)

    def test_beat_lines_do_not_pile_up_in_events(self):
        """Same reason frame lines do not: a show runs for hours."""
        show = self._show(on_frame=lambda f: None, bpm=240.0)
        time.sleep(1.5)
        self.assertFalse([e for e in show.events if e.startswith("BEAT")])
        self.assertGreater(show.beat, 0)

    def test_free_run_off_stops_the_pulse(self):
        """With nothing driving it and free-run off, the rig should settle dark."""
        frames = []
        show = self._beat_show(on_frame=frames.append, bpm=120.0, emit_rate=40.0)
        show.set_free_run(False)
        time.sleep(1.5)
        self.assertEqual(max(frames[-1][0]), 0)

    def test_the_placeholders_are_visible_and_distinct(self):
        """Every slot in the machine, which is all twelve of them for now."""
        frames = []
        show = self._show(on_frame=frames.append, emit_rate=20.0)
        seen = []
        for state in MYTHOS26_STATES:
            show.set_state(state)
            time.sleep(0.8)  # past the 0.25s cross-fade
            seen.append(tuple(frames[-1]))

        for frame in seen:
            self.assertGreater(max(max(f) for f in frame), 0, "a slot rendered black")
        self.assertEqual(len(set(seen)), len(MYTHOS26_STATES),
                         "two slots look the same")


def _rising_edges(frames, threshold=128):
    """How many times the rig went from dark to lit.

    Counting edges rather than lit frames: a pulse spans several frames and how
    many depends on the frame rate we happened to get.
    """
    lit = [max(f[0]) > threshold for f in frames]
    return sum(1 for i in range(1, len(lit)) if lit[i] and not lit[i - 1])


class BeatLooks(ShowTest):
    """The looks that fire on the beat: their envelope, and their rate.

    There used to be a divider here - on 1 / on 2 / on 4 - and it went, because
    the clock counted beats and had no idea which of them was the one, so "on
    4" fired at the right rate on an arbitrary beat of the bar with no way to
    move it. Then half time came back and seated its own pairs off the beat you
    set the rate on, which was a way to move it but not a bar: two looks in half
    time could sit on opposite beats, and the seat was counted off beat messages
    that Mixxx duplicates and drops, so it wandered between gestures.

    The bar is the clock's now - four beats from the last declared downbeat, and
    `midi align` declares one. So half time takes the one and the three of that
    bar, quarter time takes the one, and both agree with each other and with
    themselves an hour later. The one is an assumption until someone aligns it;
    that is the trade, and it is the right way round. Double time never had the
    problem - it lands on the beat and between them, whichever beat it is.
    """
    def _count(self, state, seconds=4.0, bpm=120.0, params=None):
        frames = []
        show = self.running_show(
            SHOW,pattern="audio", midi="", bpm=bpm,
            on_frame=frames.append, emit_rate=40.0
        )
        show.set_state(state)
        for name, value in (params or {}).items():
            show.set_param(name, value)
        frames.clear()          # drop the cross-fade
        time.sleep(seconds)
        return _rising_edges(frames)

    def test_beat_pulse_fires_on_every_beat(self):
        beats = 120.0 / 60.0 * 4.0
        self.assertAlmostEqual(self._count("beat_pulse"), beats, delta=2.0)

    def test_the_cue_list_sets_the_envelope(self):
        """beatLook's two numbers are what the look opens on."""
        show = self.running_show(SHOW, pattern="audio", midi="")
        show.set_state("beat_pulse")
        time.sleep(0.4)
        knobs = {p.name: p.value for p in show.params}
        self.assertAlmostEqual(knobs["attack"], 0.15, places=3)
        self.assertAlmostEqual(knobs["decay"], 0.60, places=3)

    def test_the_envelope_is_still_live(self):
        """Stated in the cue list, tunable at the desk - both, not either."""
        show = self.running_show(SHOW, pattern="audio", midi="")
        show.set_state("beat_pulse")
        time.sleep(0.4)
        show.set_param("decay", 1.25)
        time.sleep(0.3)
        knobs = {p.name: p.value for p in show.params}
        self.assertAlmostEqual(knobs["decay"], 1.25, places=3)

    def test_half_time_hits_on_every_second_beat(self):
        beats = 120.0 / 60.0 * 4.0
        self.assertAlmostEqual(self._count("beat_pulse", params={"rate": 0.5}),
                               beats / 2.0, delta=1.0)

    def test_double_time_hits_between_the_beats_too(self):
        """A short envelope, because a hit has to end before it can start again.

        At double time a beat is 250ms and the look's own envelope is 750ms, so
        counting edges on the default shape would count the one it never comes
        back down from. That is the look behaving correctly - the rate divides
        the beat, never the envelope - and it is why `decay` is right there.
        """
        beats = 120.0 / 60.0 * 4.0
        count = self._count("beat_pulse",
                            params={"attack": 0.02, "decay": 0.12, "rate": 2.0})
        self.assertAlmostEqual(count, beats * 2.0, delta=2.0)

    def test_half_time_alternates_on_a_tapped_beat(self):
        """The regression: it hit twice in a row, then skipped, on a real rig.

        Rate alone does not catch it - the count over a window stays about
        right while the *placement* wanders - so this measures the gaps between
        hits and wants every one of them two beats wide.

        Tapped rather than free-run because that is the path that broke it.
        Every tap goes through `BeatClock::markBeat` and has to come out as
        exactly one beat: one taken twice, or two taken as one, moves half time
        onto the other beat of the pair and leaves it there. Python's timing
        jitter is the hair the old version tripped over, so the taps below
        reproduce it without needing Mixxx on the other end of a cable.
        """
        period = 0.30
        stamped = []
        show = self.running_show(SHOW, pattern="audio", midi="",
                                 on_frame=lambda frame: stamped.append(
                                     (time.monotonic(), max(frame[0]) > 128)),
                                 emit_rate=40.0)
        show.set_state("beat_pulse")
        show.set_param("rate", 0.5)
        show.set_param("decay", 0.12)   # a hit that ends inside one beat
        show.set_param("attack", 0.02)

        # Ten to settle the tempo onto the tap - the clock closes a
        # quarter of the gap per beat, so ten is well inside a percent -
        # and then twelve to measure the spacing of.
        for _ in range(10):
            show.command("beat")
            time.sleep(period)
        stamped.clear()

        for _ in range(12):
            show.command("beat")
            time.sleep(period)

        edges = [now for index, (now, lit) in enumerate(stamped)
                 if lit and index > 0 and not stamped[index - 1][1]]
        self.assertGreaterEqual(len(edges), 4, "too few hits to judge the spacing")

        gaps = [b - a for a, b in zip(edges, edges[1:])]
        for gap in gaps:
            self.assertAlmostEqual(gap, period * 2, delta=period * 0.5,
                                   msg=f"hits {period * 2:.2f}s apart expected, got {gaps}")

    def test_quarter_time_hits_once_a_bar(self):
        """The rate the divider never got to keep: one hit every four beats.

        Spacing rather than a count, for the same reason as half time above -
        a look that fires four times in eight seconds is right on average even
        when it is hitting on 1, 5, 6, 10.
        """
        bpm = 200.0
        beat = 60.0 / bpm      # a bar every 1.2s, so four of them is quick
        stamped = []
        show = self.running_show(SHOW, pattern="audio", midi="", bpm=bpm,
                                 on_frame=lambda frame: stamped.append(
                                     (time.monotonic(), max(frame[0]) > 128)),
                                 emit_rate=40.0)
        show.set_state("beat_pulse")
        show.set_param("rate", 0.25)
        show.set_param("decay", 0.12)   # a hit that ends inside one beat
        show.set_param("attack", 0.02)
        time.sleep(0.5)                 # past the cue's own opening hit
        stamped.clear()
        time.sleep(5.0)

        edges = [now for index, (now, lit) in enumerate(stamped)
                 if lit and index > 0 and not stamped[index - 1][1]]
        self.assertGreaterEqual(len(edges), 3, "too few hits to judge the spacing")

        gaps = [b - a for a, b in zip(edges, edges[1:])]
        for gap in gaps:
            self.assertAlmostEqual(gap, beat * 4, delta=beat * 0.75,
                                   msg=f"hits a bar apart expected, got {gaps}")

    def test_align_says_where_the_one_is(self):
        """`midi align` is the gesture that moves the slow rates.

        A look on quarter time hits on the one, and the one is wherever the
        clock was last told it is. So aligning mid-bar should bring the hit
        forward to now rather than leaving it where the grid happened to start.
        """
        bpm = 120.0
        stamped = []
        show = self.running_show(SHOW, pattern="audio", midi="", bpm=bpm,
                                 on_frame=lambda frame: stamped.append(
                                     (time.monotonic(), max(frame[0]) > 128)),
                                 emit_rate=40.0)
        show.set_state("beat_pulse")
        show.set_param("rate", 0.25)
        show.set_param("decay", 0.12)
        show.set_param("attack", 0.02)
        time.sleep(1.4)                 # somewhere mid-bar, hit or not
        stamped.clear()
        show.command("midi align")
        aligned = time.monotonic()
        time.sleep(0.6)                 # well inside the two-second bar

        edges = [now for index, (now, lit) in enumerate(stamped)
                 if lit and index > 0 and not stamped[index - 1][1]]
        self.assertTrue(edges, "aligning the one should hit on it")
        self.assertLess(edges[0] - aligned, 0.3,
                        "the hit should land on the align, not on the old grid")

    def test_the_cue_list_sets_the_rate(self):
        """It opens on the beat; the rate is a live knob from there."""
        show = self.running_show(SHOW, pattern="audio", midi="")
        show.set_state("beat_pulse")
        time.sleep(0.4)
        knobs = {p.name: p.value for p in show.params}
        self.assertAlmostEqual(knobs["rate"], 1.0, places=3)

    def test_the_rate_snaps_to_the_musical_ones(self):
        """A slider will hand over 1.37. Nobody wants 1.37 hits a beat."""
        show = self.running_show(SHOW, pattern="audio", midi="")
        show.set_state("beat_pulse")
        time.sleep(0.4)
        for sent, landed in ((0.25, 0.25), (0.3, 0.25), (0.45, 0.5),
                             (0.5, 0.5), (0.7, 0.5), (0.9, 1.0),
                             (1.37, 1.0), (1.6, 2.0), (2.0, 2.0)):
            show.set_param("rate", sent)
            self.assertAlmostEqual(show.get_param("rate").value, landed, places=3,
                                   msg=f"sent {sent}")

    def test_beat_div_is_gone(self):
        show = self.running_show(SHOW, pattern="audio", midi="")
        with self.assertRaises(ShowError):
            show.command("beat div 2")
        self.assertTrue(show.is_running)


class TvStatic(ShowTest):
    """Every fixture a new value every frame."""

    def _frames(self, state, seconds=1.5):
        frames = []
        show = self.running_show(
            SHOW,pattern="generic", midi="",
            on_frame=frames.append, emit_rate=40.0
        )
        show.set_state(state)
        time.sleep(0.9)     # past the generic machine's 0.5s cross-fade
        frames.clear()
        time.sleep(seconds)
        self.assertTrue(frames, "no frames arrived")
        return frames

    def test_mono_is_grey(self):
        for frame in self._frames("tv_static_mono"):
            for red, green, blue in frame:
                self.assertEqual((red, green, blue), (red, red, red))

    def test_colour_is_not_grey(self):
        frames = self._frames("tv_static")
        coloured = any(
            red != green or green != blue
            for frame in frames
            for red, green, blue in frame
        )
        self.assertTrue(coloured, "every fixture came out grey")

    def test_fixtures_differ_from_each_other(self):
        """Otherwise it is a flashing rig, not static.

        The bar is "half the fixtures, up to 96 distinct values", not simply
        half: gamma is not injective on bytes, so 354 fixtures drawing uniformly
        from 256 greys and then being gamma-corrected land on about 145 distinct
        values however random they are. Asking for 177 of them tests the
        colour depth of the wire rather than whether the look is static.
        """
        frames = self._frames("tv_static_mono")
        wanted = min(len(frames[0]) // 2, 96)
        varied = sum(1 for frame in frames if len(set(frame)) > wanted)
        self.assertGreater(varied, len(frames) * 0.8)

    def test_consecutive_frames_differ(self):
        frames = self._frames("tv_static")
        same = sum(1 for i in range(1, len(frames)) if frames[i] == frames[i - 1])
        self.assertLess(same, len(frames) * 0.1)


class MidiMessageHandling(unittest.TestCase):
    """The executable's own self-test, run in synthetic time.

    This is the part of the Mixxx path that can be proved without Mixxx: given
    the messages its mapping documents, do we produce the right beats at the
    right tempo, and ignore everything else on the cable.
    """
    def test_selftest_passes(self):
        binary = executable_or_skip()
        result = subprocess.run(
            [str(binary), "--midi-selftest"], capture_output=True, text=True, timeout=30
        )

        failures = [line for line in result.stdout.splitlines() if "FAIL" in line]
        self.assertEqual(failures, [], "\n".join(failures))
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("OK midi selftest passed", result.stdout)

    def test_it_actually_checks_things(self):
        """A self-test that asserts nothing would pass just as quietly."""
        binary = executable_or_skip()
        result = subprocess.run(
            [str(binary), "--midi-selftest"], capture_output=True, text=True, timeout=30
        )
        checks = [line for line in result.stdout.splitlines() if line.startswith("SELFTEST")]
        self.assertGreaterEqual(len(checks), 12)


class IgnoredDevices(unittest.TestCase):
    """The DJ controller must never become the tempo source."""

    def test_the_show_configs_ignore_the_controller(self):
        for path in (SHOW,):
            with self.subTest(config=path.name):
                config = Config.load(path)
                self.assertTrue(config.midi.ignores("Traktor Kontrol S2 MK3"))
                self.assertFalse(config.midi.ignores("loopMIDI Port"))

    def test_a_bare_string_ignore_is_accepted(self):
        config = Config.from_dict(
            {
                "midi": {"port": "auto", "ignore": "Traktor"},
                "fixtures": [{"name": "a", "start_channel": 1}],
            }
        )
        self.assertEqual(config.midi.ignore, ["Traktor"])
        self.assertTrue(config.midi.ignores("TRAKTOR KONTROL S2"))

    def test_ignoring_the_port_you_named_is_rejected(self):
        with self.assertRaises(ConfigError):
            MidiConfig(enabled=True, port="loopMIDI", ignore=["loopmidi"]).validate()

    def test_auto_refuses_rather_than_opening_an_ignored_device(self):
        """Better a warning and free-run than a rig following a pad press."""
        executable_or_skip()

        result = subprocess.run(
            [str(executable_or_skip()), "--config", str(SHOW), "--dry-run",
             "--frames", "2", "--no-stdin"],
            capture_output=True, text=True, timeout=30,
        )

        # Only meaningful on a machine whose only MIDI input is the controller,
        # which is the machine this was written on. Elsewhere, auto may well
        # find something legitimate, and that is not a failure.
        if "every MIDI input is on midi.ignore" in result.stdout:
            self.assertIn("READY", result.stdout, "it should still light up")
            self.assertNotIn("MIDI-OPEN", result.stdout)


class MidiDiscovery(ShowTest):
    """Enumeration only - there is no guarantee of a device on any machine."""

    def test_listing_midi_inputs_does_not_fail(self):
        from eclipse_dmx.ports import list_midi_ports

        ports = list_midi_ports()
        self.assertIsInstance(ports, list)
        for port in ports:
            self.assertIsInstance(port.index, int)

    def test_opening_a_port_that_is_not_there_is_an_error_not_a_crash(self):
        show = self.running_show(SHOW, pattern="mythos26", midi="")
        with self.assertRaises(ShowError):
            show.midi_open("definitely-not-a-midi-port")
        self.assertTrue(show.is_running)


class ViewerWindow(GuiTest):
    """Geometry, read back off the real canvas rather than eyeballed."""

    def setUp(self):
        from eclipse_dmx.viewer import ViewerApp

        self.app = ViewerApp(RIG, pattern="obelisk_seasons")

    def tearDown(self):
        self.app._quit()

    # -- reading the canvas -------------------------------------------------

    @property
    def panel(self):
        """The rig's panel. Only valid after `ready()`."""
        return self.app._panels[0]

    def ready(self):
        """Wait until the rig is on the canvas.

        Two steps, and both are somebody else's clock: the panel itself is not
        built until the executable has announced what is in the show, and its
        discs are not laid out until the window manager has said how big the
        canvas is. Everything below reads one or the other, so everything below
        starts here.
        """
        self.settle_until(lambda: self.app._panels,
                          message="the executable to announce the rig")
        self.settle_until(lambda: len(self.panel._items) == 10,
                          message="ten discs on the canvas")

    def discs(self):
        """The discs' bounding boxes, once they are laid out."""
        self.ready()
        return [self.panel.canvas.coords(item["core"]) for item in self.panel._items]

    def fills(self):
        return [self.panel.canvas.itemcget(item["core"], "fill")
                for item in self.panel._items]

    def test_every_fixture_fits_at_any_window_size(self):
        from eclipse_dmx.viewer import GLOW_EXTENT

        self.ready()
        for width, height in [(1000, 420), (640, 320), (1600, 900)]:
            with self.subTest(size=(width, height)):
                self.app.root.geometry(f"{width}x{height}")
                # The resize is a request; the answer comes back from the
                # window manager, so wait for the canvas to actually be the
                # size that was asked for rather than for a fixed moment.
                self.settle_until(
                    lambda: self.panel.canvas.winfo_width() > 1
                            and abs(self.panel.canvas.winfo_width() - width) < width,
                    message=f"the canvas to be resized towards {width}x{height}")

                canvas_w = self.panel.canvas.winfo_width()
                canvas_h = self.panel.canvas.winfo_height()

                cores = self.discs()
                radius = (cores[0][2] - cores[0][0]) / 2
                glow = radius * GLOW_EXTENT

                for x0, y0, x1, y1 in cores:
                    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
                    self.assertGreaterEqual(cx - glow, -0.5)
                    self.assertLessEqual(cx + glow, canvas_w + 0.5)
                    self.assertGreaterEqual(cy - glow, -0.5)
                    self.assertLessEqual(cy + glow, canvas_h + 0.5)

    def test_glows_meet_but_do_not_overlap(self):
        from eclipse_dmx.viewer import GLOW_EXTENT

        cores = self.discs()
        centres = [((x0 + x1) / 2, (y0 + y1) / 2) for x0, y0, x1, y1 in cores]
        glow = (cores[0][2] - cores[0][0]) / 2 * GLOW_EXTENT

        gaps = [
            ((centres[i][0] - centres[i + 1][0]) ** 2 + (centres[i][1] - centres[i + 1][1]) ** 2) ** 0.5
            for i in range(len(centres) - 1)
        ]
        self.assertGreaterEqual(min(gaps), 2 * glow - 1.0)

    def test_colour_reaches_the_canvas(self):
        from eclipse_dmx.viewer import BACKGROUND

        self.discs()
        background = "#%02x%02x%02x" % BACKGROUND
        self.settle_until(
            lambda: sum(1 for fill in self.fills() if fill != background) >= 8,
            message="the frames to reach eight of the ten discs")
        self.assertGreater(len(set(self.fills())), 2)

    def test_blackout_paints_the_rig_dark(self):
        self.discs()
        self.app._toggle_blackout()
        self.settle_until(lambda: all(fill == "#000000" for fill in self.fills()),
                          message="every disc to go dark")

    def test_pattern_cycling(self):
        self.discs()
        before = self.app.current_pattern
        self.app._step_pattern(1)
        self.settle_until(lambda: self.app.current_pattern != before,
                          message="the next pattern to come up")
        self.assertEqual(self.app._status, "")


class ViewerOnTheObelisk(GuiTest):
    """344 pixels in a window that was built for ten pars."""

    def setUp(self):
        from eclipse_dmx.viewer import ViewerApp

        self.app = ViewerApp(OBELISK, pattern="obelisk_seasons")

    def tearDown(self):
        self.app._quit()

    # -- reading the canvas -------------------------------------------------

    @property
    def panel(self):
        """The sculpture's panel. Only valid after `ready()`."""
        return self.app._panels[0]

    def ready(self):
        """Wait until all 344 pixels are on the canvas.

        The panel is not built until the executable has announced the rig, and
        its pixels are not laid out until the window manager has said how big
        the canvas is - two waits on somebody else's clock, which is why this
        is a condition rather than a number.
        """
        self.settle_until(lambda: self.app._panels,
                          message="the executable to announce the sculpture")
        self.settle_until(lambda: len(self.panel._items) == OBELISK_PIXELS,
                          message=lambda: f"{OBELISK_PIXELS} pixels on the canvas "
                                          f"(have {len(self.panel._items)})")

    def fills(self):
        return [self.panel.canvas.itemcget(item["core"], "fill")
                for item in self.panel._items]

    def test_every_pixel_is_drawn_once(self):
        self.ready()

    def test_dense_rigs_drop_the_glow(self):
        """3096 canvas items per repaint does not fit in a frame."""
        self.ready()
        self.assertTrue(all(not item["rings"] for item in self.panel._items))

    def test_pixels_stay_inside_the_canvas(self):
        self.ready()
        for width, height in [(1000, 600), (640, 320), (1600, 900)]:
            with self.subTest(size=(width, height)):
                self.app.root.geometry(f"{width}x{height}")
                # The resize is a request the window manager answers, so wait
                # for the canvas to have moved rather than for a fixed moment.
                self.settle_until(
                    lambda: self.panel.canvas.winfo_width() > 1
                            and abs(self.panel.canvas.winfo_width() - width) < width,
                    message=f"the canvas to be resized towards {width}x{height}")

                canvas_w = self.panel.canvas.winfo_width()
                canvas_h = self.panel.canvas.winfo_height()
                for item in self.panel._items:
                    x0, y0, x1, y1 = self.panel.canvas.coords(item["core"])
                    self.assertGreaterEqual(x0, -0.5)
                    self.assertLessEqual(x1, canvas_w + 0.5)
                    self.assertGreaterEqual(y0, -0.5)
                    self.assertLessEqual(y1, canvas_h + 0.5)

    def test_the_four_sides_are_visibly_different(self):
        """The picture shows what the coordinates promised."""
        from eclipse_dmx.viewer import BACKGROUND

        self.ready()
        background = "#%02x%02x%02x" % BACKGROUND

        def four_seasons():
            fills = self.fills()
            if fills[0] == background:
                return None
            sides = [fills[side * 2 * OBELISK_SIDE_LENGTH + 20] for side in range(4)]
            return sides if len(set(sides)) == 4 else None

        # The seasons are a gradient around the sculpture and the look opens
        # dark, so this is waiting for the rig to be lit *and* for the four
        # sides to have pulled apart - which is the assertion, said once.
        self.settle_until(four_seasons,
                          message=lambda: "four distinct sides, got "
                                          f"{[self.fills()[s * 2 * OBELISK_SIDE_LENGTH + 20] for s in range(4)]}")

    def test_a_repeated_frame_is_not_repainted(self):
        """The pump runs at 60Hz over a 30fps stream; half of it is redundant."""
        self.ready()
        painted = self.settle_until(lambda: self.app._painted,
                                    message="a frame to be painted")

        self.app._paint(painted)
        self.assertIs(self.app._painted, painted)

    def test_the_link_row_is_only_there_for_a_relic(self):
        """A dead row of buttons on a DMX rig is furniture that does nothing."""
        self.ready()
        self.assertFalse(self.app.link_row.winfo_ismapped())

    def test_runs_are_labelled_rather_than_pixels(self):
        """Eight legends, not 344."""
        self.ready()
        canvas = self.panel.canvas
        self.settle_until(
            lambda: sorted(canvas.itemcget(item, "text")
                           for item in canvas.find_all()
                           if canvas.type(item) == "text")
                    == ["a_down", "a_up", "b_down", "b_up",
                        "c_down", "c_up", "d_down", "d_up"],
            message="one legend per run, and only those")


class ViewerOnARelic(GuiTest):
    """The relic row, on a config that names one.

    Dry-run, so no cable is opened and the executable is on `console` - the row
    is drawn from the *config*, which is what a desk needs to see before it
    plugs anything in.
    """
    def setUp(self):
        from eclipse_dmx.viewer import ViewerApp

        self.app = ViewerApp(OBELISK_USB, pattern="obelisk_seasons")

    def tearDown(self):
        self.app._quit()

    def test_the_row_is_shown(self):
        self.settle(0.8)
        self.assertTrue(self.app.link_row.winfo_ismapped())
        self.assertEqual(sorted(self.app._link_buttons), ["cue", "pixels", "release"])

    def test_pixel_mode_is_lit_to_start(self):
        from eclipse_dmx.viewer import BUTTON_BG_ACTIVE

        self.settle(0.8)
        self.assertEqual(self.app._link_buttons["pixels"]["bg"], BUTTON_BG_ACTIVE)

    def test_release_is_never_lit(self):
        """It is an action, not a mode."""
        from eclipse_dmx.viewer import BUTTON_BG

        self.settle(0.8)
        self.assertEqual(self.app._link_buttons["release"]["bg"], BUTTON_BG)


class MidiLearn(GuiTest):
    """Binding a pad by hitting it, down the path a real pad takes.

    Events are fed as monitor lines rather than as MidiEvents, because that is
    what arrives: the executable prints `MIDI-IN ch=1 note_on 41 100`, the
    reader thread queues it, and the pump offers it to learn before the
    mappings. A test that skipped to take_learn would not cover the half of
    this that has ever been wrong.
    """
    def setUp(self):
        import tempfile
        from eclipse_dmx.viewer import ViewerApp

        # Made before the viewer, because the map is loaded in the constructor
        # from config/midimaps/default.json - the checkout's own, which is a
        # real file on a machine that has ever run a set. These tests start
        # from an empty map, so the viewer is pointed at a path in here that
        # does not exist yet rather than at whatever is bound tonight.
        self.scratch = tempfile.TemporaryDirectory()
        scratch_map = Path(self.scratch.name) / "default.json"

        # `generic`, because the mapping these tests fire opens `tv_static`,
        # which is a cue on that machine - on a pattern that does not offer it
        # "the cue did not open" would be true whatever learn did.
        self.app = ViewerApp(SHOW, pattern="generic", midi="", bpm=120.0, midimap=scratch_map)

        # The panel saves on the way out when it is dirty, and learning makes
        # it dirty. `path` cleared so the save goes through default_dir, which
        # is what a first-run map does and is the path the last test checks -
        # proving the directory is the one the viewer handed it rather than a
        # hardcoded one.
        self.panel = self.app.midi_panel
        self.panel.path = None
        self.panel.default_dir = Path(self.scratch.name)

    def tearDown(self):
        self.app._quit()
        self.scratch.cleanup()

    def hit(self, *lines):
        for line in lines:
            self.app._on_midi_line(line)
        self.settle(0.3)

    def test_learn_from_an_empty_map_needs_no_typing(self):
        # The first-run flow: nothing bound, nothing selected, and the editor
        # (with its own learn button) is not even on screen yet.
        self.settle(0.6)
        self.assertEqual(self.panel.mappings.mappings, [])

        self.panel._add_and_learn()
        self.settle(0.2)
        self.assertTrue(self.panel._learning)

        self.hit("ch=3 note_on 41 100", "ch=3 note_off 41 0")

        bound = self.panel.mappings.mappings[-1]
        self.assertEqual((bound.kind, bound.channel, bound.number), ("note", 3, 41))
        self.assertFalse(self.panel._learning, "one pad, then disarmed")

    def test_the_pad_being_learned_does_not_also_fire(self):
        # Hitting a pad to bind it must not launch the cue it is being bound
        # to - the point of taking the event before the dispatcher sees it.
        from eclipse_dmx.midi_map import Mapping

        self.panel.mappings.mappings.append(
            Mapping(label="cue", kind="note", channel=3, number=41,
                    action="state", params={"name": "tv_static"}))
        self.panel._refresh_list()
        self.panel._select_index(0)
        self.settle(0.4)
        opened = self.app.show.current_state

        self.panel._toggle_learn()
        self.hit("ch=3 note_on 41 100", "ch=3 note_off 41 0")

        self.assertEqual(self.app.show.current_state, opened)

    def test_the_release_is_not_a_second_binding(self):
        # A pad speaks twice. Learning the up-stroke would bind the release
        # and the next thing touched would land on the wrong mapping.
        self.settle(0.6)
        self.panel._add_and_learn()
        self.hit("ch=1 note_on 60 100")
        self.assertFalse(self.panel._learning)
        first = self.panel.mappings.mappings[-1].number

        self.hit("ch=1 note_off 60 0")           # arrives after learn ended
        self.assertEqual(self.panel.mappings.mappings[-1].number, first)
        self.assertEqual(len(self.panel.mappings.mappings), 1)

    def test_a_knob_binds_as_a_cc(self):
        self.settle(0.6)
        self.panel._add_and_learn()
        self.hit("ch=1 cc 74 64")
        bound = self.panel.mappings.mappings[-1]
        self.assertEqual((bound.kind, bound.channel, bound.number), ("cc", 1, 74))

    def test_selecting_another_mapping_cancels_the_arm(self):
        # Otherwise the next pad binds to whatever was clicked, which is not
        # what the click meant.
        self.settle(0.6)
        self.panel._add()
        self.panel._add()
        self.panel._toggle_learn()
        self.assertTrue(self.panel._learning)

        self.panel._list.selection_clear(0, "end")
        self.panel._list.selection_set(0)
        self.panel._on_select()
        self.settle(0.2)

        self.assertFalse(self.panel._learning)

    def test_an_evenings_bindings_survive_the_window_closing(self):
        # save_if_dirty on the way out, into the directory the viewer named.
        self.settle(0.6)
        self.panel._add_and_learn()
        self.hit("ch=2 note_on 36 100")
        self.app._quit()

        written = Path(self.scratch.name) / "default.json"
        self.assertTrue(written.exists(), "learned bindings were not kept")
        reloaded = midi_map.MappingSet.load(written)
        self.assertEqual(reloaded.mappings[-1].number, 36)


class MidiFind(GuiTest):
    """Learn read backwards: hit a pad, and its mapping is the one selected.

    Down the same path as learn - monitor line, reader thread, pump - because
    the half of this that can break is the same half: the event has to reach
    the panel before the dispatcher, and it has to be eaten rather than passed
    on once it does.
    """
    def setUp(self):
        import tempfile
        from eclipse_dmx.viewer import ViewerApp

        self.scratch = tempfile.TemporaryDirectory()
        scratch_map = Path(self.scratch.name) / "default.json"
        self.app = ViewerApp(SHOW, pattern="generic", midi="", bpm=120.0,
                             midimap=scratch_map)
        self.panel = self.app.midi_panel
        self.panel.path = None
        self.panel.default_dir = Path(self.scratch.name)

    def tearDown(self):
        self.app._quit()
        self.scratch.cleanup()

    def hit(self, *lines):
        for line in lines:
            self.app._on_midi_line(line)
        self.settle(0.3)

    def bind(self, *triggers):
        """Mappings on the panel's map, in the order given."""
        from eclipse_dmx.midi_map import Mapping

        for label, channel, number, state, page in triggers:
            self.panel.mappings.mappings.append(
                Mapping(label=label, kind="note", channel=channel, number=number,
                        page=page, action="state", params={"name": state}))
        self.panel._refresh_list()
        self.panel._select_index(0)
        self.settle(0.4)

    def test_hitting_a_pad_selects_its_mapping(self):
        self.bind(("first", 1, 40, "tv_static", ""),
                  ("second", 1, 41, "tv_static_mono", ""))

        self.panel._toggle_find()
        self.assertTrue(self.panel._finding)

        self.hit("ch=1 note_on 41 100", "ch=1 note_off 41 0")

        self.assertIs(self.panel._selected, self.panel.mappings.mappings[1])
        self.assertEqual(self.panel._list.curselection(), (1,))
        self.assertFalse(self.panel._finding, "one pad, then disarmed")

    def test_the_pad_being_found_does_not_also_fire(self):
        """Asking what a button does must not do it."""
        self.bind(("cue", 3, 41, "tv_static", ""))
        opened = self.app.show.current_state

        self.panel._toggle_find()
        self.hit("ch=3 note_on 41 100", "ch=3 note_off 41 0")

        self.assertEqual(self.app.show.current_state, opened)

    def test_an_unbound_pad_says_so_and_disarms(self):
        self.bind(("first", 1, 40, "tv_static", ""))

        self.panel._toggle_find()
        self.hit("ch=1 note_on 77 100", "ch=1 note_off 77 0")

        self.assertFalse(self.panel._finding)
        self.assertIn("nothing bound", self.app._status)

    def test_one_pad_with_two_bindings_steps_through_them(self):
        """A pad bound once per page is normal; find walks the pair."""
        self.bind(("on the show page", 1, 41, "tv_static", "generic"),
                  ("on the audio page", 1, 41, "tv_static_mono", "audio"))

        self.panel._toggle_find()
        self.hit("ch=1 note_on 41 100", "ch=1 note_off 41 0")
        first = self.panel._selected

        self.panel._toggle_find()
        self.hit("ch=1 note_on 41 100", "ch=1 note_off 41 0")
        second = self.panel._selected

        self.assertIsNot(first, second, "the same row twice, not the pair")
        self.assertEqual({id(first), id(second)},
                         {id(row) for row in self.panel.mappings.mappings})

    def test_a_knob_finds_its_cc_binding(self):
        from eclipse_dmx.midi_map import Mapping

        self.panel.mappings.mappings.append(
            Mapping(label="fader", kind="cc", channel=1, number=74, mode="value",
                    action="master", params={}))
        self.panel._refresh_list()
        self.settle(0.3)

        self.panel._toggle_find()
        self.hit("ch=1 cc 74 64")

        self.assertIs(self.panel._selected, self.panel.mappings.mappings[0])

    def test_selecting_a_row_cancels_the_arm(self):
        self.bind(("first", 1, 40, "tv_static", ""),
                  ("second", 1, 41, "tv_static_mono", ""))

        self.panel._toggle_find()
        self.panel._list.selection_clear(0, "end")
        self.panel._list.selection_set(0)
        self.panel._on_select()
        self.settle(0.2)

        self.assertFalse(self.panel._finding)

    def test_arming_learn_disarms_find(self):
        """Two armed modes on one wire is one mode too many."""
        self.bind(("first", 1, 40, "tv_static", ""))

        self.panel._toggle_find()
        self.panel._toggle_learn()

        self.assertTrue(self.panel._learning)
        self.assertFalse(self.panel._finding)


class MidiMapEditorActions(GuiTest):
    """A mapping that does several things, driven through the editor.

    The list is the point of the shape - one pad, a scene on the visualiser
    and a state on this rig - so the tests that matter are the ones about a
    second block: that it lands on the second action rather than the first,
    that removing it puts the row back, and that the last one cannot go.
    """
    def setUp(self):
        import tempfile
        from eclipse_dmx.viewer import ViewerApp

        self.scratch = tempfile.TemporaryDirectory()
        self.app = ViewerApp(SHOW, pattern="mythos26", midi="", bpm=120.0,
                             midimap=Path(self.scratch.name) / "default.json")
        self.panel = self.app.midi_panel
        self.panel.path = None
        self.panel.default_dir = Path(self.scratch.name)

        self.settle(0.5)
        self.panel._add()
        self.settle(0.2)
        self.mapping = self.panel.mappings.mappings[-1]

    def tearDown(self):
        self.app._quit()
        self.scratch.cleanup()

    def block(self, index):
        return self.panel._action_rows[index]

    def test_a_fresh_mapping_has_exactly_one_action(self):
        self.assertEqual(len(self.mapping.actions), 1)
        self.assertEqual(len(self.panel._action_rows), 1)

    def test_a_second_action_is_the_other_half_of_the_desk(self):
        # + do offers `state` rather than a copy of the first, because the
        # second half of a cue is nearly always the rig.
        self.panel._add_action()
        self.settle(0.2)
        self.assertEqual([a.key for a in self.mapping.actions],
                         ["syn_scene", "state"])
        self.assertEqual(len(self.panel._action_rows), 2)

    def test_typing_in_the_second_block_lands_on_the_second_action(self):
        # The cross-copy risk: two blocks with fields of the same name - both
        # `state` actions have `name` - written back onto the wrong action.
        self.panel._add_action()
        self.settle(0.2)
        self.block(0).param_vars["scene"].set("Neon Grid")
        self.block(1).param_vars["name"].set("tv_static")
        self.panel._commit()

        self.assertEqual(self.mapping.actions[0].params["scene"], "Neon Grid")
        self.assertEqual(self.mapping.actions[1].params["name"], "tv_static")

    def test_a_removed_action_takes_its_block_with_it(self):
        self.panel._add_action()
        self.settle(0.2)
        self.block(1).param_vars["name"].set("tv_static")
        self.panel._commit()

        self.panel._remove_action(1)
        self.settle(0.2)

        self.assertEqual([a.key for a in self.mapping.actions], ["syn_scene"])
        self.assertEqual(len(self.panel._action_rows), 1)

    def test_the_last_action_cannot_be_removed(self):
        # A trigger that does nothing has no spelling in the file format, and
        # `del` is what removes a row.
        self.panel._remove_action(0)
        self.settle(0.2)
        self.assertEqual(len(self.mapping.actions), 1)

    def test_changing_a_kind_takes_that_kind_s_defaults(self):
        # The old parameters named fields the new action does not have, so
        # they are dropped rather than carried across.
        self.block(0).param_vars["scene"].set("Neon Grid")
        self.panel._commit()

        self.block(0).action_var.set(midi_map.ACTIONS["syn_favslot"].label)
        self.panel._on_action_kind(0)
        self.settle(0.2)

        self.assertEqual(self.mapping.actions[0].key, "syn_favslot")
        self.assertEqual(self.mapping.actions[0].params, {"slot": 1})
        self.assertNotIn("scene", self.mapping.actions[0].params)

    def test_both_halves_fire_from_one_pad(self):
        # End to end: the row the editor built, down the path a pad takes.
        # Through the form, not onto the mapping - while a row is selected the
        # form is what a commit writes back, so an assignment behind it would
        # be undone by the next one.
        self.panel._kind_var.set("note")
        self.panel._channel_var.set("1")
        self.panel._number_var.set("41")
        self.panel._mode_var.set("press")
        self.panel._add_action()
        self.settle(0.2)
        self.block(0).param_vars["scene"].set("Neon Grid")
        self.block(1).param_vars["name"].set(self.app.show.state_names[-1])
        self.panel._commit()
        self.settle(0.2)

        self.app._on_midi_line("ch=1 note_on 41 100")
        self.settle(0.5)

        self.assertEqual(self.app.show.current_state, self.app.show.state_names[-1])

    def test_the_list_line_says_a_row_grew(self):
        self.panel._add_action()
        self.settle(0.2)
        line = self.panel._list.get(self.panel.mappings.mappings.index(self.mapping))
        self.assertIn("2 actions", line)


class TheFrameSink(ShowTest):
    """Client mode's far end, run locally: paint what arrives, render nothing.

    The contract is that a frame survives the trip byte for byte. It has to:
    an F line is read back out of the universe at the sending end, so it is
    already through master, brightness and gamma, and anything this end did to
    it again would darken the rig by a curve on every hop.
    """
    def setUp(self):
        self.frames = []
        self.events = []
        self.sink = ShowController(
            SHOW, dry_run=True, autostart=False, sink=True,
            emit_rate=30, on_frame=self.frames.append, on_event=self.events.append)
        self.sink.start()
        self.settle(1.0)

    def tearDown(self):
        try:
            self.sink.stop()
        except Exception:
            pass

    def fixture_count(self):
        line = next(e for e in self.events if e.startswith("SINK "))
        return int(line.split()[1])

    def test_it_announces_how_many_fixtures_it_will_paint(self):
        # The desk and this end have to agree on the shape of a frame without
        # negotiating it; both read it off the same config.
        self.assertGreater(self.fixture_count(), 0)
        self.assertEqual(self.fixture_count(), len(self.sink.fixture_names))

    def test_a_frame_survives_the_trip_byte_for_byte(self):
        n = self.fixture_count()
        sent = [((i * 7) % 256, (i * 13) % 256, (i * 29) % 256) for i in range(n)]

        for _ in range(12):
            self.sink.send_frame(sent)
            self.settle(0.05)
        self.settle(0.5)

        self.assertTrue(self.frames, "the sink echoed nothing")
        self.assertEqual(self.frames[-1], sent,
                         "what came back is not what went in")

    def test_it_paints_the_newest_frame_not_a_backlog(self):
        # Several frames can land between two renders. A dropped one is 30ms
        # of the last picture; a queue of stale ones is the rig running late.
        n = self.fixture_count()
        for shade in range(0, 60):
            self.sink.send_frame([(shade, 0, 0)] * n)
        self.settle(0.8)
        self.assertEqual(self.frames[-1][0], (59, 0, 0))

    def test_a_short_frame_is_taken_as_far_as_it_goes(self):
        # A picture, not a transaction: half of one beats none, and the next
        # is 30ms away.
        n = self.fixture_count()
        self.sink.send_frame([(255, 0, 0)] * n)
        self.settle(0.3)
        self.sink.send_frame([(0, 255, 0)] * (n // 2))
        self.settle(0.5)

        painted = self.frames[-1]
        self.assertEqual(painted[0], (0, 255, 0), "the half it was given")
        self.assertEqual(painted[-1], (255, 0, 0), "the rest left as it was")

    def test_send_frame_needs_a_sink(self):
        show = ShowController(SHOW, dry_run=True, autostart=False)
        with self.assertRaises(ShowError):
            show.send_frame([(0, 0, 0)])


class ViewerGivesTheDevicesBack(GuiTest):
    """What the desk lets go of on the way out.

    Closing the window was never the only door: Ctrl-C is what the launcher
    tells you to use, and it unwinds straight past `_quit` out of mainloop.
    Leaving that way used to leave the Launchpad in Programmer mode with the
    map still lit and its own Setup button disabled - a controller you have to
    unplug to get back.
    """
    def setUp(self):
        import tempfile
        from eclipse_dmx.viewer import ViewerApp
        self.scratch = tempfile.TemporaryDirectory()
        self.app = ViewerApp(SHOW, pattern="mythos26", midi="", bpm=120.0,
                             midimap=Path(self.scratch.name) / "default.json")
        self.settle(0.6)

        # Stand in for a controller that was opened, without needing one.
        self.sent = []
        self.closed = []
        self.commands = []
        self.app._lamps_open = True
        self.app.show.midi_send = self.sent.append
        self.app.show.midi_out_close = lambda: self.closed.append("closed")
        real_command = self.app.show.command
        def spy(line, expect_reply=True):
            self.commands.append(line)
            if line.startswith("midi "):
                return "OK"
            return real_command(line, expect_reply)
        self.app.show.command = spy

    def tearDown(self):
        self.app._quit()
        self.scratch.cleanup()

    def test_it_puts_the_controller_back_in_live_mode(self):
        self.app._release_devices()

        self.assertTrue(self.sent, "nothing was sent to the controller")
        # Programmer mode off is the one that matters: it re-enables the
        # device's own Setup button.
        self.assertIn(launchpad.programmer_mode(False), self.sent)
        self.assertEqual(self.closed, ["closed"])

    def test_it_darkens_the_pads_before_letting_go(self):
        self.app._release_devices()

        clears = launchpad.clear()
        for message in clears:
            self.assertIn(message, self.sent)
        # and the mode change comes after the dark, not before
        self.assertLess(self.sent.index(clears[0]),
                        self.sent.index(launchpad.programmer_mode(False)))

    def test_it_hands_the_input_port_back(self):
        self.app._release_devices()
        self.assertIn("midi close", self.commands)

    def test_the_second_way_out_is_a_no_op(self):
        # _quit runs it, and run()'s finally runs it again. The second must be
        # silent rather than a traceback over a clean exit.
        self.app._release_devices()
        before = (len(self.sent), len(self.closed), self.commands.count("midi close"))

        self.app._release_devices()

        self.assertEqual(len(self.sent), before[0])
        self.assertEqual(len(self.closed), before[1])
        self.assertEqual(self.commands.count("midi close"), before[2])

    def test_a_controller_that_was_never_opened_is_left_alone(self):
        self.app._lamps_open = False
        self.sent.clear()

        self.app._release_devices()

        self.assertEqual(self.sent, [], "nothing to put back")
        self.assertIn("midi close", self.commands, "the input is still handed back")


class ViewerAgainstAnotherRig(GuiTest):
    """A rig whose device list is not this config's.

    Which is what `--host` is: the executable runs on the pi, out of the pi's
    checkout, and its `config/mythos26.json` has a scanner_ring this desk's
    copy does not. The frame that comes back is 391 fixtures with the obelisk
    at 35; the config here says 356 with the obelisk at 0.

    The panels are built from the DEVICE lines and skip what the config has no
    positions for - so they are *not* parallel to the spans, and an index into
    one was never an index into the other. That drew every panel one device
    early: the obelisk panel showing the ring's pixels.
    """
    def setUp(self):
        import tempfile
        from eclipse_dmx.viewer import ViewerApp
        self.scratch = tempfile.TemporaryDirectory()
        self.app = ViewerApp(SHOW, pattern="mythos26", midi="", bpm=120.0,
                             midimap=Path(self.scratch.name) / "default.json")
        self.settle(0.8)

    def tearDown(self):
        self.app._quit()
        self.scratch.cleanup()

    def test_the_local_rig_draws_each_panel_its_own_slice(self):
        spans = list(self.app.show.devices)
        self.assertTrue(spans, "the show announced no devices")
        for panel in self.app._panels:
            self.assertIsNotNone(getattr(panel, "span", None), panel.name)
            self.assertEqual(panel.span.name, panel.name)

    def test_a_device_this_config_lacks_does_not_shift_the_others(self):
        from eclipse_dmx.controller import DeviceSpan

        real = list(self.app.show.devices)
        names = [span.name for span in real]
        self.assertIn("obelisk", names, "the test show has no obelisk")
        first_real = next(s for s in real if s.name == "obelisk").first

        # The pi's shape: an extra device first, everything else pushed along.
        # Deliberately a name no config will ever have - `scanner_ring` was
        # the real case and is now in this config, which is the point: the
        # test is about a device the desk cannot draw, not about that one.
        extra = 35
        shifted = [DeviceSpan(index=0, name="ghost_ring", first=0, count=extra)]
        for span in real:
            shifted.append(DeviceSpan(index=span.index + 1, name=span.name,
                                      first=span.first + extra, count=span.count))
        self.app.show.devices = shifted
        self.app._rebuild_items()
        self.settle(0.3)

        drawn = {panel.name: panel.span for panel in self.app._panels}
        self.assertNotIn("ghost_ring", drawn, "no positions for it here")
        for span in shifted[1:]:
            self.assertIn(span.name, drawn)
            self.assertEqual(drawn[span.name].first, span.first,
                             f"{span.name} must read from where the rig put it")

        self.assertEqual(drawn["obelisk"].first, first_real + extra,
                         "the obelisk reads from where the rig put it, not from before")

    def test_the_slice_each_panel_paints_is_its_own(self):
        from eclipse_dmx.controller import DeviceSpan

        real = list(self.app.show.devices)
        extra = 4
        shifted = [DeviceSpan(index=0, name="ghost", first=0, count=extra)]
        for span in real:
            shifted.append(DeviceSpan(index=span.index + 1, name=span.name,
                                      first=span.first + extra, count=span.count))
        self.app.show.devices = shifted
        self.app._rebuild_items()
        self.settle(0.3)

        # A frame whose every pixel encodes its own index, so a slice that
        # started in the wrong place is visible in the values. The screen's
        # gamma correction is off for this: it rewrites every channel, which
        # is right for a picture and fatal to a frame used as a ruler.
        self.app._ungamma = None
        total = extra + sum(span.count for span in real)
        frame = [((i >> 8) & 0xFF, i & 0xFF, 0) for i in range(total)]

        painted = {}
        for panel in self.app._panels:
            panel.paint = (lambda got, name=panel.name: painted.__setitem__(name, got))

        self.app._paint(frame, force=True)

        for panel_name, got in painted.items():
            span = next(s for s in shifted if s.name == panel_name)
            first = (got[0][0] << 8) | got[0][1]
            self.assertEqual(first, span.first,
                             f"{panel_name} began at {first}, rig put it at {span.first}")
            self.assertEqual(len(got), span.count)


class ViewerOnTheShow(GuiTest):
    """The tempo controls, driven the way a click drives them."""

    def setUp(self):
        from eclipse_dmx.viewer import ViewerApp

        # Opened on the audio bus and put straight onto the beat flash: the
        # panel tests below read a look with a float, a bool and a colour on
        # it, and while the show's own list is twelve empty slots the flash is
        # a cue here - see beatPulseState.
        self.app = ViewerApp(SHOW, pattern="audio", midi="", bpm=120.0)
        self.app._run_button(("state", "beat_pulse"))

    def tearDown(self):
        self.app._quit()

    # -- what every test here starts by waiting for -------------------------

    def knobs(self):
        """Wait until the cue's knobs have reached the panel.

        The widgets are built from what the executable announces about the
        cue, which is a round trip away, so nothing below can touch a knob
        until this has happened. It is the same wait every time, so it is
        written once and named for what it is waiting for.
        """
        return self.settle_until(lambda: self.app._param_widgets,
                                 message="the cue's knobs to reach the panel")

    #: The header's tempo is redrawn off BEAT lines, so its floor is a beat or
    #: two of the *show's* clock rather than anything this test controls - and
    #: several, on a machine also running the rest of the suite. Waiting longer
    #: costs nothing when it lands early, which is the normal case; the ceiling
    #: only decides how quickly a genuine failure is called.
    HEADER_PATIENCE = 8.0

    def header_says(self, text, timeout=HEADER_PATIENCE):
        return self.settle_until(
            lambda: text in self.app.header.cget("text"), timeout,
            # What it says instead, and whether the desk complained: a tempo
            # the executable refused and one it simply has not announced yet
            # look identical from here otherwise.
            lambda: f"the header to say {text!r} - it says "
                    f"{self.app.header.cget('text')!r}, "
                    f"status {self.app._status!r}, "
                    f"clock {self.app.show.bpm} {self.app.show.beat_source}")

    def clock_running(self, beats=2):
        """Wait until the show's clock has actually counted a few beats.

        Not padding, and not politeness. A `bpm` sent inside the first beat is
        taken - the reply is OK and `status` reports the new tempo - but the
        free-run clock then stops emitting BEAT lines altogether, so nothing
        downstream ever hears the tempo it just accepted. Reproducible on the
        controller alone, with no viewer in it:

            show.set_state("beat_pulse")
            wait for the first BEAT line, then `bpm 100`
            -> status says bpm=100, and no beat ever arrives again

        Waiting out a beat or two first is what the old flat `settle(0.8)`
        here was doing without saying so. It is written down now because the
        next person to tighten this wait will otherwise find the same wall.
        """
        return self.settle_until(lambda: self.app.show.beat >= beats,
                                 message=lambda: f"the clock to count {beats} beats "
                                                 f"(at {self.app.show.beat})")

    def param(self, name):
        """What the executable currently holds for the knob `name`."""
        return self.app.show.get_param(name).value

    def assertNoComplaint(self):
        self.assertEqual(self.app._status, "")

    # -- the tempo controls -------------------------------------------------

    def test_the_show_states_have_buttons(self):
        self.settle_until(
            lambda: all(state in self.app._state_buttons
                        for state in MYTHOS26_STATES),
            message="a button for every state the show declares")

    def test_the_tempo_reaches_the_header(self):
        self.header_says("120.0 bpm")

    def test_the_tempo_buttons_take(self):
        self.clock_running()
        self.app._run_button(("bpm", "100"))
        self.header_says("100.0 bpm")
        self.assertNoComplaint()

    def test_tapping_does_not_error(self):
        """And the header picks up that the clock is following the tap.

        The beat line re-announces on its own cadence, so "manual" lands in
        the header a beat or two after the tap - at 120bpm, up to a second
        away. Waited out rather than slept past, because the one sleep that
        always wins the race does not exist.
        """
        self.clock_running()
        self.app._run_button(("beat", ""))
        self.assertNoComplaint()
        self.header_says("manual")

    def test_the_master_slider_reaches_the_rig(self):
        self.knobs()
        self.app._on_master_slider("40")
        self.settle_until(lambda: abs(self.app._master - 0.4) < 0.005,
                          message="the master to reach 0.4")
        self.assertNoComplaint()
        self.assertIn("master=0.4", self.app.show.status())

    def test_the_arrow_keys_and_the_slider_agree(self):
        """Two controls on one value; they must not drift apart."""
        self.knobs()
        self.app._on_master_slider("50")
        self.settle_until(lambda: abs(self.app._master - 0.5) < 0.005,
                          message="the master to reach 0.5")
        self.app._nudge_master(0.1)
        self.settle_until(lambda: abs(self.app._master - 0.6) < 0.005,
                          message="the nudge to reach the master")
        self.assertAlmostEqual(self.app._master_var.get(), 60.0, delta=1.0)

    def test_dragging_the_slider_does_not_feed_itself(self):
        """_set_master writes the variable back, which re-enters the handler."""
        self.knobs()
        self.app._on_master_slider("70")
        self.settle_until(lambda: abs(self.app._master - 0.7) < 0.005,
                          message="the master to reach 0.7")
        self.assertNoComplaint()

    # -- the knobs ----------------------------------------------------------

    def test_the_looks_knobs_get_controls(self):
        self.knobs()
        self.assertIn("attack", self.app._param_widgets)
        self.assertIn("hold", self.app._param_widgets)

        # a float gets a slider and a box; a bool gets neither box nor range
        self.assertIsNotNone(self.app._param_widgets["attack"][1])
        self.assertIsNone(self.app._param_widgets["hold"][1])

    def test_a_cue_change_rebuilds_the_panel(self):
        self.knobs()
        self.assertNotIn("gain", self.app._param_widgets)

        self.app._run_button(("state", "level"))
        self.settle_until(lambda: "gain" in self.app._param_widgets,
                          message="the new cue's knobs to replace the old ones")

        self.assertNotIn("attack", self.app._param_widgets)
        self.assertNoComplaint()

    def test_a_colour_knob_gets_a_swatch_of_its_own_colour(self):
        self.knobs()
        _, swatch = self.app._param_widgets["color"]
        self.assertIsNotNone(swatch)
        self.assertEqual(str(swatch.cget("bg")), "#ffffff")

    def test_a_picked_colour_reaches_the_look_and_the_swatch(self):
        """The picker itself is the system's; everything either side is ours."""
        from eclipse_dmx import viewer

        self.knobs()
        picked = []

        def fake_picker(*args, **kwargs):
            picked.append(kwargs.get("color"))
            return ((0, 64, 255), "#0040ff")

        original = viewer.colorchooser.askcolor
        viewer.colorchooser.askcolor = fake_picker
        try:
            self.app._pick_color("color")
            self.settle_until(lambda: self.param("color") == "#0040ff",
                              message="the picked colour to reach the look")
        finally:
            viewer.colorchooser.askcolor = original

        # Opened on the colour the look is actually showing, not on a default.
        self.assertEqual(picked, ["#ffffff"])
        self.assertEqual(str(self.app._param_widgets["color"][1].cget("bg")), "#0040ff")
        self.assertNoComplaint()

    def test_a_cancelled_pick_changes_nothing(self):
        from eclipse_dmx import viewer

        self.knobs()
        before = self.param("color")

        original = viewer.colorchooser.askcolor
        viewer.colorchooser.askcolor = lambda *args, **kwargs: (None, None)
        try:
            self.app._pick_color("color")
            # No condition to wait for: the assertion is that nothing happens,
            # so this one has to spend the time and look afterwards.
            self.settle(0.4)
        finally:
            viewer.colorchooser.askcolor = original

        self.assertEqual(self.param("color"), before)

    def test_a_slider_reaches_the_look(self):
        self.knobs()
        self.app._on_param_slider("floor", "0.75")
        self.settle_until(lambda: abs(self.param("floor") - 0.75) < 0.005,
                          message="the slider to reach the look")
        self.assertNoComplaint()

    def test_the_box_takes_a_value_the_slider_cannot_land_on(self):
        """The whole reason there is a box beside the slider."""
        self.knobs()
        _, entry = self.app._param_widgets["decay"]
        entry.delete(0, "end")
        entry.insert(0, "0.137")
        self.app._on_param_entry("decay")
        self.settle_until(lambda: abs(self.param("decay") - 0.137) < 0.0005,
                          message="the typed value to reach the look")

    def test_nonsense_in_the_box_puts_the_real_value_back(self):
        self.knobs()
        _, entry = self.app._param_widgets["decay"]
        live = self.param("decay")

        entry.delete(0, "end")
        entry.insert(0, "banana")
        self.app._on_param_entry("decay")
        # The box is put back on the spot, but the pump is what runs the
        # handler's redraw, so it still needs a turn or two of the loop.
        self.settle_until(lambda: entry.get() != "banana",
                          message="the box to put the real value back")

        self.assertAlmostEqual(float(entry.get()), live, places=3)
        self.assertAlmostEqual(self.param("decay"), live, places=3)


class TheEchoAgainstTheAnnouncement(unittest.TestCase):
    """A `param` answered before the cue's announcement has been closed off.

    A look announces its knobs as a block - `PARAMS`, then one `PARAM` a knob -
    and the block is closed by the first line that is not part of it. That is
    almost always a frame, so it was left at that. Almost: a knob set straight
    after a cue change can be answered before the next frame is written, and
    then the echo arrives with the block still open and is read as another
    knob rather than as an answer about one already there.

    Fed line by line rather than through the executable, because the whole
    point is the *order* of two lines with nothing in between - which a real
    show only produces on a race that a test cannot ask for.
    """

    def feed(self, *lines):
        show = ShowController(SHOW, dry_run=True, autostart=False,
                              on_frame=lambda frame: None)
        for line in lines:
            show._handle_stdout(line)
        return show

    ANNOUNCEMENT = (
        "PARAMS audio beat_pulse",
        "PARAM attack f 0.15 0 1",
        "PARAM decay f 0.60 0 4",
    )

    def test_an_echo_inside_the_block_is_not_a_second_knob(self):
        show = self.feed(*self.ANNOUNCEMENT, "PARAM decay f 0.11 0 4")
        self.assertEqual([param.name for param in show.params],
                         ["attack", "decay"])

    def test_the_desk_reads_back_the_value_it_just_set(self):
        """The bug this is here for: `get_param` takes the first match, so the
        duplicate left the *announced* value in front of the echo and a slider
        moved in that window reported where it had been."""
        show = self.feed(*self.ANNOUNCEMENT, "PARAM decay f 0.11 0 4")
        self.assertAlmostEqual(show.get_param("decay").value, 0.11, places=3)

    def test_the_echo_closes_the_block(self):
        """It proved the announcement was over, so the revision moves and the
        defaults snapshot lands - the same as if a frame had closed it."""
        show = self.feed(*self.ANNOUNCEMENT, "PARAM decay f 0.11 0 4")
        self.assertEqual(show.params_revision, 1)

    def test_the_defaults_are_the_announced_values_not_the_echo(self):
        """reset_look() replays the cue's own construction. A value the desk
        set a moment later is not part of it."""
        show = self.feed(*self.ANNOUNCEMENT, "PARAM decay f 0.11 0 4")
        defaults, _curves = show._look_defaults[("audio", "beat_pulse")]
        self.assertAlmostEqual(defaults["decay"], 0.60, places=3)

    def test_a_frame_still_closes_the_block_the_way_it_always_did(self):
        show = self.feed(*self.ANNOUNCEMENT, "F 0 0 0")
        self.assertEqual(show.params_revision, 1)
        self.assertEqual([param.name for param in show.params],
                         ["attack", "decay"])

    def test_a_new_knob_inside_the_block_is_still_a_knob(self):
        """The name is what tells an echo from an announcement, so a block
        with distinct names has to keep behaving as one."""
        show = self.feed(*self.ANNOUNCEMENT, "PARAM floor f 0.0 0 1")
        self.assertEqual([param.name for param in show.params],
                         ["attack", "decay", "floor"])
        self.assertEqual(show.params_revision, 0, "the block is not over yet")

    def test_a_layer_reads_its_own_block_the_same_way(self):
        """LayerView carries its own copy of this, and so its own copy of the
        bug: an additive layer's knobs are set from the same desk."""
        show = self.feed(
            "LAYER uv PARAMS audio flash",
            "LAYER uv PARAM gain f 0.50 0 1",
            "LAYER uv PARAM gain f 0.25 0 1",
        )
        layer = show.layers["uv"]
        self.assertEqual([param.name for param in layer.params], ["gain"])
        self.assertAlmostEqual(layer.get_param("gain").value, 0.25, places=3)


class LookParams(ShowTest):
    """The per-look tuning surface, over the protocol."""

    #: The beat flash's knobs, in the order the look declares them. Named once
    #: because it is both what one test asserts and what every other test has
    #: to wait for: a cue change is announced knob by knob, so "some knobs have
    #: arrived" is a list with half of the old cue's still in it.
    FLASH_KNOBS = ["attack", "decay", "intensity", "floor",
                   "hold", "rate", "entry_hit", "color"]

    def _show(self, **kwargs):
        """The beat flash, running, with its knobs already announced.

        Read off the beat flash, which has one of everything - a float, a bool
        and a colour. It is a cue on the audio bus while the show's own list is
        empty, so it is asked for rather than opened on.

        The wait for the announcement lives here rather than at the top of
        every test: the knobs are the whole subject, so there is nothing any of
        them can do before it has happened, and waiting for the announcement
        beats sleeping the six-tenths that used to stand in for it.
        """
        show = self.running_show(SHOW, pattern="audio", midi="",
                                 emit_rate=20.0, **kwargs)
        since = show.params_revision
        show.set_state("beat_pulse")
        self.until_knobs(show, self.FLASH_KNOBS, since)
        return show

    def test_the_running_look_announces_its_knobs(self):
        show = self._show()
        names = [param.name for param in show.params]
        self.assertEqual(names, self.FLASH_KNOBS)

        attack = show.get_param("attack")
        self.assertEqual(attack.kind, "f")
        self.assertFalse(attack.is_bool)
        self.assertTrue(show.get_param("hold").is_bool)
        self.assertTrue(show.get_param("color").is_color)

    def test_a_cue_change_replaces_the_set(self):
        """The knobs belong to the look, not to the pattern."""
        show = self._show()
        first = show.params_revision

        self.assertIn("attack", [param.name for param in show.params])

        show.set_state("level")
        self.settle_until(lambda: show.params_revision > first,
                          message="the new cue's knobs to replace the old ones")
        names = [param.name for param in show.params]
        self.assertIn("gain", names)
        self.assertNotIn("attack", names, "the flash's knobs stayed behind")

    def test_a_value_out_of_range_is_clamped_not_refused(self):
        show = self._show()
        show.set_param("attack", 99.0)
        self.assertAlmostEqual(show.get_param("attack").value, 1.0, places=3)
        show.set_param("attack", -5.0)
        self.assertAlmostEqual(show.get_param("attack").value, 0.0, places=3)

    def test_the_echo_lands_before_the_reply(self):
        """A client that waits on OK and then reads must see the new value.

        Emitted the other way round, this passes about half the time, which is
        the worst kind of protocol bug to own.
        """
        show = self._show()
        for target in (0.11, 0.22, 0.33, 0.44):
            show.set_param("decay", target)
            self.assertAlmostEqual(show.get_param("decay").value, target, places=3)

    def test_setting_a_knob_does_not_duplicate_the_set(self):
        """The echo updates in place; it does not append to the list."""
        show = self._show()
        before = len(show.params)
        show.set_param("floor", 0.5)
        show.set_param("floor", 0.6)
        # The echo rides the frame stream, so a duplicate would show up a beat
        # late rather than never. Wait for the second value to land, then count.
        self.settle_until(lambda: show.get_param("floor").value == 0.6,
                          message="the second set to echo back")
        self.assertEqual(len(show.params), before)

    def test_a_knob_reaches_the_render(self):
        """The point of the whole thing: the value is the look's own field."""
        frames = []
        show = self.running_show(SHOW, pattern="audio", midi="", bpm=128.0,
                                 on_frame=frames.append, emit_rate=40.0)
        since = show.params_revision
        show.set_state("beat_pulse")
        self.until_knobs(show, self.FLASH_KNOBS, since)

        # Both readings are a *minimum over a window*, because the floor is the
        # trough of the envelope - so the window has to be real show time.
        # 0.8s at 128bpm is a beat and a half of it. Only the coming-up either
        # side is waited for rather than slept through.
        frames.clear()
        self.settle(0.8)
        dark = min(max(frame[0]) for frame in frames)

        show.set_param("floor", 0.8)
        frames.clear()
        self.settle(0.8)
        lifted = min(max(frame[0]) for frame in frames)

        self.assertLess(dark, 40)
        self.assertGreater(lifted, 120)

    def test_a_bool_takes_on_off_and_reaches_the_render(self):
        frames = []
        show = self.running_show(SHOW, pattern="generic", midi="",
                                 on_frame=frames.append, emit_rate=40.0)
        show.set_state("tv_static_mono")
        self.settle(1.0)     # past the generic machine's 0.5s cross-fade
        self.assertTrue(show.get_param("monochrome").value)

        show.set_param("monochrome", False)
        time.sleep(0.3)
        frames.clear()
        time.sleep(0.5)

        self.assertFalse(show.get_param("monochrome").value)
        self.assertTrue(any(len(set(colour)) > 1 for frame in frames for colour in frame),
                        "still grey after monochrome was turned off")

    def test_a_name_that_is_not_a_knob_is_rejected_without_dying(self):
        show = self._show()
        with self.assertRaises(ShowError):
            show.set_param("not_a_knob", 1.0)
        self.assertTrue(show.is_running)

    def test_dump_is_the_running_look_as_json(self):
        show = self._show()
        show.set_param("attack", 0.25)
        dump = json.loads(show.dump_params())
        self.assertAlmostEqual(dump["attack"], 0.25, places=3)
        self.assertIsInstance(dump["hold"], bool)

    def test_a_colour_knob_round_trips_as_hex(self):
        show = self._show()
        self.assertEqual(show.get_param("color").value, "#ffffff")

        show.set_param("color", "#0040ff")
        self.assertEqual(show.get_param("color").value, "#0040ff")

    def _peak_colour(self, state, knobs):
        """The lit frame's colour, on the rig, with those knobs set."""
        frames = []
        show = self.running_show(SHOW, pattern="audio", midi="", bpm=128.0,
                                 on_frame=frames.append, emit_rate=40.0)
        show.set_state(state)
        for name, value in knobs.items():
            show.set_param(name, value)
        self.settle(0.8)         # past the cross-fade
        frames.clear()
        self.settle(1.0)         # a beat and change, to catch a lit frame
        self.assertTrue(frames, "no frames arrived")
        return max(frames, key=lambda frame: max(frame[0]))[0]

    def test_a_colour_knob_reaches_the_render(self):
        red, green, blue = self._peak_colour("beat_pulse", {"color": "#00ff00"})
        self.assertGreater(green, 200)
        self.assertLess(red, 40)
        self.assertLess(blue, 40)

    def test_a_colour_that_is_not_one_is_rejected_without_dying(self):
        show = self._show()
        with self.assertRaises(ShowError):
            show.set_param("color", "banana")
        self.assertTrue(show.is_running)

        # And the other way round: a number is not a colour either.
        with self.assertRaises(ShowError):
            show.set_param("color", 0.5)
        self.assertTrue(show.is_running)

    def test_a_hex_string_is_not_a_number_knob(self):
        """`param attack #ff0000` is a mistake, not a conversion."""
        show = self._show()
        with self.assertRaises(ShowError):
            show.set_param("attack", "#ff0000")
        self.assertTrue(show.is_running)

    def test_dump_keeps_a_colour_as_a_string(self):
        """The point of a dump is that it can be pasted back into a config."""
        show = self._show()
        show.set_param("color", "#123456")
        dump = json.loads(show.dump_params())
        self.assertEqual(dump["color"], "#123456")

    def test_a_plain_pattern_offers_the_three_it_has(self):
        show = self._show()
        show.set_pattern("rainbow")
        self.settle_until(
            lambda: [param.name for param in show.params] ==
                    ["speed", "width", "brightness"],
            message="the plain pattern's three knobs")


class ScannerKnobs(ShowTest):
    """The scanner looks' tuning surface - the knobs the curve editor drives."""

    def _show(self, **kwargs):
        return self.running_show(SCANNER, midi="", emit_rate=20.0, **kwargs)

    def cue(self, show, state, knobs):
        """Put `show` on `state` and wait for exactly `knobs` to be announced.

        Which is also the assertion: `until_knobs` fails, with both lists in
        the message, if the cue announces anything else. It used to be a flat
        0.8s sleep and an assertEqual, which cost the same whether the
        announcement took 60ms or nearly all of it.
        """
        since = show.params_revision
        show.set_state(state)
        self.until_knobs(show, knobs, since)

    def test_the_boot_swell_has_two_independent_knobs(self):
        self.cue(self._show(), "boot", ["boot_time", "noise"])

    def test_the_recording_states_share_one_wave_and_its_knobs(self):
        """Unified look, unified surface: both states offer the same wave."""
        wave = ["wave_scale", "rise_speed"]
        show = self._show()
        self.cue(show, "scan_item_detected_filter", wave)
        self.cue(show, "audio_playback_recording", wave)

    def test_the_recording_flow_offers_its_shapes(self):
        show = self._show()

        # the breath, and how much of the tower it gives to the shadow
        self.cue(show, "record_arm", ["rate", "floor", "gain", "shadow"])

        # the fire's own knobs, plus the douse the game cues
        self.cue(show, "cleanse_arm",
                 ["cooling", "sparking", "spread", "rise", "emitter", "douse"])

        self.cue(show, "record_active", ["orbit_rate", "tail"])
        self.cue(show, "record_countdown", ["sweep", "edge", "floor"])

    def test_a_scanner_knob_reaches_the_render(self):
        """floor 1, gain 0 flattens record_saved's breath to a steady green."""
        frames = []
        show = self._show(on_frame=frames.append)
        self.cue(show, "record_saved", ["rate", "floor", "gain", "shadow"])

        show.set_param("floor", 1.0)
        show.set_param("gain", 0.0)

        # The knobs are announced before the cue has finished blending in, so
        # the flat green has to be waited for rather than assumed - and only
        # then is the span worth measuring. "Steady" is a claim about a span,
        # so that part still has to watch one.
        self.settle_until(lambda: frames and frames[-1][0][1] > 200,
                          timeout=6.0, message="the cue to blend up to full green")
        frames.clear()
        self.settle(0.8)

        greens = [frame[0][1] for frame in frames]
        self.assertGreater(min(greens), 200, "not at full green")
        self.assertLessEqual(max(greens) - min(greens), 2, "still breathing")

    def test_the_countdown_fills_the_ring_and_the_douse_puts_the_fire_out(self):
        """The ring's top pixel lights first and its last pixel last; after
        the douse cue nothing new ignites, so the fire goes dark."""
        frames = []
        show = self._show(on_frame=frames.append)

        self.cue(show, "record_countdown", ["sweep", "edge", "floor"])
        # pixel 0 is the top of the ring, pixel 34 just before it
        early = self.settle_until(lambda: frames and frames[-1][0][0] > 100,
                                  message="the top pixel to light")
        self.assertLess(frames[-1][34][0], 40, "the last pixel is lit early")

        # The fill is the look's own three seconds; there is nothing to do but
        # watch it happen.
        self.settle_until(lambda: frames[-1][34][0] > 200, timeout=6.0,
                          message="the ring to fill")

        self.cue(show, "cleanse_arm",
                 ["cooling", "sparking", "spread", "rise", "emitter", "douse"])
        self.settle(1.0)             # let the fire catch before dousing it
        show.trigger("scanner.cleanse.douse")

        # Nothing new ignites after the douse, so this is the longest flame
        # already alight burning out - real time, and no event to wait on.
        self.settle(4.5)
        frames.clear()
        self.settle(0.5)
        brightest = max(max(channel for pixel in frame[:35] for channel in pixel)
                        for frame in frames)
        self.assertLess(brightest, 8, "the fire is still burning after the douse")


class TheCurveProtocol(ShowTest):
    """Live shapes over the wire: CURVE announcements and the `curve` command.

    The point of the whole channel: a look's envelope is an AutomationCurve,
    and the desk's curve editor loads the live shape and writes an edited one
    back - so the protocol must announce shapes with the knobs and accept a
    whole shape at once.
    """
    def test_the_pulse_announces_its_envelope(self):
        show = self.running_show(SHOW, pattern="audio", midi="", emit_rate=20.0)
        # The shape under test is the beat flash's, which is a cue on the
        # bus while the show's own list is empty - see beatPulseState.
        show.set_state("beat_pulse")
        time.sleep(0.6)
        self.assertIn("envelope", show.curves)
        self.assertGreaterEqual(len(show.curves["envelope"]), 3)

    def test_a_drawn_shape_lands_and_echoes(self):
        show = self.running_show(SHOW, pattern="audio", midi="", emit_rate=20.0)
        # The shape under test is the beat flash's, which is a cue on the
        # bus while the show's own list is empty - see beatPulseState.
        show.set_state("beat_pulse")
        time.sleep(0.6)
        show.set_curve("envelope", [
            (0.0, 0.0, None),
            (0.1, 1.0, "EaseOutCubic"),
            (0.6, 0.0, None),
        ])

        keys = show.curves["envelope"]
        self.assertEqual(len(keys), 3)
        self.assertAlmostEqual(keys[1][0], 0.1, places=4)
        self.assertAlmostEqual(keys[1][1], 1.0, places=4)
        self.assertEqual(keys[1][2], "EaseOutCubic")
        self.assertIsNone(keys[0][2])

    def test_a_shape_reaches_the_render(self):
        """Flatten the boot swell's brightness curve to zero: the rig darkens."""
        frames = []
        show = self.running_show(SCANNER, midi="", on_frame=frames.append,
                              emit_rate=20.0)
        show.set_state("boot")
        time.sleep(1.2)
        self.assertIn("brightness", show.curves)
        self.assertIn("noise", show.curves)

        show.set_curve("brightness", [(0.0, 0.0, None), (1.0, 0.0, None)])
        time.sleep(0.3)
        frames.clear()
        time.sleep(0.5)

        self.assertTrue(frames)
        self.assertLess(max(max(color) for frame in frames for color in frame), 20,
                        "still lit after the swell was drawn flat")

    def test_an_unknown_curve_is_refused_without_dying(self):
        show = self.running_show(SHOW, pattern="audio", midi="", emit_rate=20.0)
        # The shape under test is the beat flash's, which is a cue on the
        # bus while the show's own list is empty - see beatPulseState.
        show.set_state("beat_pulse")
        time.sleep(0.6)
        with self.assertRaises(ShowError):
            show.set_curve("not_a_curve", [(0.0, 0.0, None), (1.0, 1.0, None)])
        self.assertTrue(show.is_running)

    def test_the_announcement_lands_whole(self):
        """When the revision moves, the knobs AND curves are all there.

        The revision used to bump on the PARAMS header, so a UI polling it
        could rebuild from a half-filled list and a just-cleared curve dict -
        which read as the envelope target flickering out of the aim menu.
        """
        show = self.running_show(SHOW, pattern="audio", midi="", emit_rate=20.0)
        # The shape under test is the beat flash's, which is a cue on the
        # bus while the show's own list is empty - see beatPulseState.
        show.set_state("beat_pulse")
        for _ in range(100):
            if show.params_revision > 0:
                break
            time.sleep(0.05)

        self.assertGreater(show.params_revision, 0)
        self.assertTrue(show.params, "revision moved before the knobs landed")
        self.assertIn("envelope", show.curves,
                      "revision moved before the curves landed")

    def test_reset_restores_the_cue(self):
        """Tune a knob and redraw the envelope; reset puts both back."""
        show = self.running_show(SHOW, pattern="audio", midi="", emit_rate=20.0)
        # The shape under test is the beat flash's, which is a cue on the
        # bus while the show's own list is empty - see beatPulseState.
        show.set_state("beat_pulse")
        time.sleep(0.6)

        show.set_param("attack", 0.4)
        show.set_curve("envelope", [(0.0, 0.0, None), (0.9, 1.0, None),
                                    (2.0, 0.0, None)])
        self.assertAlmostEqual(show.get_param("attack").value, 0.4, places=3)

        show.reset_look()

        # the cue constructs attack 0.15; reset is the cue, not zero
        self.assertAlmostEqual(show.get_param("attack").value, 0.15, places=3)
        self.assertAlmostEqual(show.curves["envelope"][1][0], 0.15, places=3)

    def test_a_ninth_key_is_refused_whole(self):
        """Too many keys rejects the message; the look keeps its old shape."""
        show = self.running_show(SHOW, pattern="audio", midi="", emit_rate=20.0)
        # The shape under test is the beat flash's, which is a cue on the
        # bus while the show's own list is empty - see beatPulseState.
        show.set_state("beat_pulse")
        time.sleep(0.6)
        before = show.curves["envelope"]
        with self.assertRaises(ShowError):
            show.set_curve("envelope",
                           [(i * 0.1, 0.5, None) for i in range(9)])
        self.assertEqual(show.curves["envelope"], before)


class TheCurveMirror(unittest.TestCase):
    """curves.py must agree with eanim::AutomationCurve, line for line.

    The editor previews what the sculpture will play, so any drift between the
    two is a curve that was tuned against the wrong maths.
    """
    def test_an_empty_curve_is_zero(self):
        self.assertEqual(Curve().evaluate(1.0), 0.0)

    def test_one_key_is_that_value_everywhere(self):
        curve = Curve()
        curve.add_key(1.0, 0.7)
        for seconds in (0.0, 1.0, 5.0):
            self.assertAlmostEqual(curve.evaluate(seconds), 0.7)

    def test_the_span_clamps_at_both_ends(self):
        curve = Curve()
        curve.add_key(1.0, 0.2)
        curve.add_key(2.0, 0.8)
        self.assertAlmostEqual(curve.evaluate(0.0), 0.2)
        self.assertAlmostEqual(curve.evaluate(9.0), 0.8)

    def test_a_segment_without_easing_is_linear(self):
        curve = Curve()
        curve.add_key(0.0, 0.0)
        curve.add_key(2.0, 1.0)
        self.assertAlmostEqual(curve.evaluate(0.5), 0.25)

    def test_two_keys_at_one_time_step_and_the_later_wins(self):
        curve = Curve()
        curve.add_key(0.0, 0.0)
        curve.add_key(1.0, 1.0)
        curve.add_key(1.0, 0.3)
        curve.add_key(2.0, 0.3)
        self.assertAlmostEqual(curve.evaluate(1.0), 0.3)

    def test_easing_shapes_the_segment_it_leaves(self):
        curve = Curve()
        curve.add_key(0.0, 0.0, "EaseInQuad")
        curve.add_key(1.0, 1.0)
        # alpha 0.5 through t*t is 0.25
        self.assertAlmostEqual(curve.evaluate(0.5), 0.25)

    def test_keys_sort_on_the_way_in(self):
        curve = Curve()
        curve.add_key(2.0, 0.2)
        curve.add_key(0.5, 0.5)
        curve.add_key(1.0, 1.0)
        self.assertEqual([key.time for key in curve.keys], [0.5, 1.0, 2.0])
        self.assertAlmostEqual(curve.duration, 2.0)

    def test_the_ninth_key_is_refused_like_the_sculptures(self):
        curve = Curve()
        for index in range(Curve.MAX_KEYS):
            self.assertTrue(curve.add_key(float(index), 0.5))
        self.assertFalse(curve.add_key(99.0, 0.5))
        self.assertEqual(len(curve.keys), Curve.MAX_KEYS)

    def test_the_cpp_round_trip_is_addkey_calls(self):
        curve = Curve()
        curve.add_key(0.0, 0.0, "EaseOutCubic")
        curve.add_key(1.5, 1.0)
        self.assertEqual(
            curve.to_cpp("hit").splitlines(),
            ["hit.clear();",
             "hit.addKey(0.000f, 0.000f, easing_functions::EaseOutCubic);",
             "hit.addKey(1.500f, 1.000f);"])

    def test_the_example_fits_on_a_relic(self):
        curve = example_hit()
        self.assertTrue(0 < len(curve.keys) <= Curve.MAX_KEYS)
        self.assertAlmostEqual(curve.evaluate(0.0), 0.0)
        self.assertGreater(curve.duration, 0.0)


class TheEasingPort(unittest.TestCase):
    """Golden values from external/easing.cpp compiled by the pinned g++.

    Including the library's own two accidents, mirrored on purpose: the
    unsequenced double decrement in easeInOutCubic (every operand ends up
    seeing t-2, so the upper half is 1+4(t-2)^3 and dives negative), and the
    bounce trio calling the *int* abs so the sine truncates away. The editor
    must draw what the binary plays, accidents included.
    """

    #: (enum name, alpha, what the compiled getEasingFunction returned)
    GOLDEN = [
        ("EaseInSine", 0.25, 0.3826834262),
        ("EaseOutQuad", 0.75, 0.9375000000),
        ("EaseInOutSine", 0.5, 0.5000000000),
        ("EaseOutCubic", 0.1, 0.2710000000),
        ("EaseInExpo", 0.9, 0.5726799586),
        ("EaseInOutCirc", 0.6, 0.7236067977),
        ("EaseOutBack", 0.5, 1.0876975000),
        ("EaseOutElastic", 0.25, 1.2923212582),
        # the accidents
        ("EaseInOutCubic", 0.5, -12.5),
        ("EaseInOutCubic", 0.75, -6.8125),
        ("EaseInBounce", 0.9, 0.0),
        ("EaseOutBounce", 0.25, 1.0),
        ("EaseInOutBounce", 0.5, 0.5),
    ]

    def test_the_port_matches_the_binary(self):
        for name, alpha, want in self.GOLDEN:
            self.assertAlmostEqual(apply_easing(name, alpha), want, places=9,
                                   msg=f"{name} at {alpha}")

    def test_linear_and_nonsense_pass_through(self):
        self.assertEqual(apply_easing("linear", 0.3), 0.3)
        self.assertEqual(apply_easing("NotAnEasing", 0.3), 0.3)

    def test_the_table_is_the_enum(self):
        # index order is what a C++ curve would store, so it must be complete
        self.assertEqual(len(EASING_NAMES), 30)
        self.assertEqual(EASING_NAMES[0], "EaseInSine")
        self.assertEqual(EASING_NAMES[-1], "EaseInOutBounce")


class OscBeyondFloats(unittest.TestCase):
    """The routes that carry names and slots, not colours."""

    def test_strings_are_tagged_and_padded(self):
        packet = osc.encode("/presets", "Deep")
        # address, then ",s", then the string, every block 4-aligned
        self.assertIn(b"/presets\0", packet)
        self.assertIn(b",s\0", packet)
        self.assertTrue(packet.endswith(b"Deep\0\0\0\0"))
        self.assertEqual(len(packet) % 4, 0)

    def test_ints_are_big_endian_int32(self):
        packet = osc.encode("/x", 3)
        self.assertIn(b",i\0", packet)
        self.assertTrue(packet.endswith(b"\x00\x00\x00\x03"))

    def test_floats_still_floats(self):
        packet = osc.encode("/x", 0.5)
        self.assertIn(b",f\0", packet)

    def test_mixed_arguments_keep_their_order(self):
        packet = osc.encode("/x", 1, "a", 0.5)
        self.assertIn(b",isf\0", packet)

    def test_scene_address_folds_the_name(self):
        # lowercase, and spaces/underscores/hyphens removed - the app's rule
        self.assertEqual(osc.scene_address("Neon Grid"), "/scenes/neongrid")
        self.assertEqual(osc.scene_address("mecha_wave-2"), "/scenes/mechawave2")


class _RecorderLink:
    """Stands in for SynesthesiaLink; remembers instead of sending."""

    def __init__(self):
        self.calls = []

    def send_scene(self, scene, preset=None):
        self.calls.append(("scene", scene, preset))

    def send_preset(self, preset):
        self.calls.append(("preset", preset))

    def send_favslot(self, slot):
        self.calls.append(("favslot", slot))

    def send_media(self, name):
        self.calls.append(("media", name))

    def send_raw(self, address, *values):
        self.calls.append(("raw", address) + values)


class _RecorderRig:
    """Stands in for ShowController on the rig side of an action: remembers
    the calls, in the order they arrived, so ordering can be asserted."""

    def __init__(self):
        self.log = []
        self.params = []

    @property
    def states(self):
        return [line.split(" ", 1)[1] for line in self.log if line.startswith("state ")]

    def set_state(self, name):
        self.log.append(f"state {name}")

    def set_pattern(self, name):
        self.log.append(f"pattern {name}")

    def command(self, line, expect_reply=True):
        self.log.append(f"command {line}")


class TheMidiMap(unittest.TestCase):
    """The mapping model: what a monitor line becomes, and what it fires."""

    def _dispatcher(self, mappings):
        link = _RecorderLink()
        context = midi_map.ActionContext(show=None, osc_factory=lambda: link)
        return midi_map.Dispatcher(midi_map.MappingSet(mappings), context), link

    def _rig_dispatcher(self, mappings):
        """A dispatcher with both halves of the desk wired to recorders."""
        rig, link = _RecorderRig(), _RecorderLink()
        context = midi_map.ActionContext(show=rig, osc_factory=lambda: link)
        return midi_map.Dispatcher(midi_map.MappingSet(mappings), context), rig, link

    def test_a_monitor_line_parses(self):
        event = midi_map.parse_midi_line("ch=1 cc 40 127")
        self.assertEqual((event.kind, event.channel, event.data1, event.data2),
                        ("cc", 1, 40, 127))

    def test_monitor_noise_is_none_not_a_crash(self):
        # the monitor's own overflow note, and anything else non-message
        self.assertIsNone(midi_map.parse_midi_line("dropped 3"))
        self.assertIsNone(midi_map.parse_midi_line("ch=x cc 40 127"))
        self.assertIsNone(midi_map.parse_midi_line(""))

    def test_press_fires_on_the_down_stroke_only(self):
        mapping = midi_map.Mapping(kind="cc", channel=0, number=40, mode="press",
                                   action="syn_scene", params={"scene": "Neon Grid"})
        down = midi_map.parse_midi_line("ch=1 cc 40 127")
        up = midi_map.parse_midi_line("ch=1 cc 40 0")
        self.assertEqual(mapping.fires_on(down), 1.0)
        self.assertIsNone(mapping.fires_on(up))

    def test_release_is_the_other_edge(self):
        mapping = midi_map.Mapping(kind="note", number=60, mode="release",
                                   action="syn_preset", params={"preset": "x"})
        self.assertIsNone(mapping.fires_on(midi_map.parse_midi_line("ch=1 note_on 60 100")))
        self.assertEqual(mapping.fires_on(midi_map.parse_midi_line("ch=1 note_off 60 0")), 1.0)
        # a pad that releases as note_on velocity 0 is the same up-stroke
        self.assertEqual(mapping.fires_on(midi_map.parse_midi_line("ch=1 note_on 60 0")), 1.0)

    def test_value_carries_the_fader(self):
        mapping = midi_map.Mapping(kind="cc", number=7, mode="value",
                                   action="syn_control", params={"address": "/x"})
        value = mapping.fires_on(midi_map.parse_midi_line("ch=1 cc 7 64"))
        self.assertAlmostEqual(value, 64 / 127.0)

    def test_channel_zero_is_any_channel(self):
        anywhere = midi_map.Mapping(kind="note", channel=0, number=60)
        somewhere = midi_map.Mapping(kind="note", channel=2, number=60)
        event = midi_map.parse_midi_line("ch=5 note_on 60 100")
        self.assertTrue(anywhere.matches(event))
        self.assertFalse(somewhere.matches(event))

    def test_round_trips_through_json(self):
        mapping = midi_map.Mapping(label="drop", kind="cc", channel=3, number=41,
                                   mode="press", action="syn_scene",
                                   params={"scene": "Neon Grid", "preset": "Deep"},
                                   enabled=False)
        blob = json.dumps(midi_map.MappingSet([mapping]).to_dict())
        back = midi_map.MappingSet.from_dict(json.loads(blob)).mappings[0]
        self.assertEqual(back.to_dict(), mapping.to_dict())

    def test_a_press_sends_scene_and_preset(self):
        dispatcher, link = self._dispatcher([
            midi_map.Mapping(kind="note", number=36, mode="press",
                             action="syn_scene",
                             params={"scene": "Neon Grid", "preset": "Deep"}),
        ])
        said = dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 36 90"))
        self.assertEqual(link.calls, [("scene", "Neon Grid", "Deep")])
        self.assertTrue(said and "Neon Grid" in said[0])

    def test_a_fader_scales_into_its_range(self):
        dispatcher, link = self._dispatcher([
            midi_map.Mapping(kind="cc", number=7, mode="value", action="syn_control",
                             params={"address": "/controls/global/slider/1",
                                     "low": 0.0, "high": 2.0}),
        ])
        dispatcher.handle(midi_map.parse_midi_line("ch=1 cc 7 127"))
        self.assertEqual(link.calls, [("raw", "/controls/global/slider/1", 2.0)])

    def test_a_disabled_mapping_is_silent(self):
        dispatcher, link = self._dispatcher([
            midi_map.Mapping(kind="note", number=36, action="syn_favslot",
                             params={"slot": 2}, enabled=False),
        ])
        dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 36 90"))
        self.assertEqual(link.calls, [])

    def test_one_broken_mapping_does_not_stop_the_next(self):
        # the first has no show to talk to; the second must still send
        dispatcher, link = self._dispatcher([
            midi_map.Mapping(kind="note", number=36, action="state",
                             params={"name": "campfire"}),
            midi_map.Mapping(kind="note", number=36, action="syn_favslot",
                             params={"slot": 3}),
        ])
        said = dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 36 90"))
        self.assertEqual(link.calls, [("favslot", 3)])
        self.assertEqual(len(said), 2)

    def test_an_unknown_action_is_a_line_not_a_crash(self):
        dispatcher, _link = self._dispatcher([
            midi_map.Mapping(kind="note", number=36, action="warp_core"),
        ])
        said = dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 36 90"))
        self.assertIn("warp_core", said[0])

    def test_load_coerces_a_hand_edited_slot(self):
        # a string where an int belongs must come back an int, not a TypeError
        # at showtime
        data = {"mappings": [{"trigger": {"kind": "note", "channel": 0, "number": 36},
                              "action": "syn_favslot", "params": {"slot": "4"}}]}
        back = midi_map.MappingSet.from_dict(data).mappings[0]
        self.assertEqual(back.actions[0].params["slot"], 4)

    # -- a row that does several things ------------------------------------

    def test_one_pad_sets_a_scene_and_a_state(self):
        # The reason actions are a list: a cue is both halves of the desk, and
        # to whoever hits the pad that is one thing.
        dispatcher, rig, link = self._rig_dispatcher([
            midi_map.Mapping(label="cue A", kind="note", number=41, actions=[
                midi_map.Action("syn_scene", {"scene": "Neon Grid", "preset": ""}),
                midi_map.Action("state", {"name": "tv_static"}),
            ]),
        ])

        said = dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 41 100"))

        self.assertEqual(link.calls, [("scene", "Neon Grid", None)])
        self.assertEqual(rig.states, ["tv_static"])
        self.assertEqual(len(said), 2)

    def test_the_actions_run_in_the_order_written(self):
        # A state then a knob is a knob turned on the look the state just
        # brought up, so the order in the file is not decoration.
        dispatcher, rig, _link = self._rig_dispatcher([
            midi_map.Mapping(kind="note", number=41, actions=[
                midi_map.Action("state", {"name": "tv_static"}),
                midi_map.Action("command", {"line": "bpm 128"}),
            ]),
        ])

        dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 41 100"))

        self.assertEqual(rig.log, ["state tv_static", "command bpm 128"])

    def test_one_broken_action_does_not_stop_its_neighbours(self):
        # Same argument as across rows, one level down: a dead visualiser must
        # not cost the rig the half of the cue that was going to work.
        dispatcher, rig, _link = self._rig_dispatcher([
            midi_map.Mapping(label="cue", kind="note", number=41, actions=[
                midi_map.Action("warp_core", {}),
                midi_map.Action("state", {"name": "tv_static"}),
            ]),
        ])

        said = dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 41 100"))

        self.assertEqual(rig.states, ["tv_static"], "the good half still ran")
        self.assertIn("warp_core", said[0])

    def test_a_version_1_file_still_opens(self):
        # Maps written before actions were a list. Read, not migrated on disk:
        # they only take the new shape when something saves them.
        data = {"version": 1, "mappings": [
            {"label": "drop", "trigger": {"kind": "note", "channel": 3, "number": 41},
             "mode": "press", "action": "syn_scene",
             "params": {"scene": "Neon Grid", "preset": "Deep"}, "enabled": True}]}
        back = midi_map.MappingSet.from_dict(data).mappings[0]

        self.assertEqual(len(back.actions), 1)
        self.assertEqual(back.actions[0].key, "syn_scene")
        self.assertEqual(back.actions[0].params["scene"], "Neon Grid")
        self.assertEqual(back.number, 41)
        # and saving it writes the new shape
        self.assertEqual(midi_map.MappingSet([back]).to_dict()["version"], 2)

    def test_a_row_with_no_actions_still_has_one(self):
        # A trigger that does nothing is not something the editor can draw a
        # form for, so the file format does not have a way to spell it.
        back = midi_map.MappingSet.from_dict(
            {"version": 2, "mappings": [{"label": "empty", "actions": []}]}).mappings[0]
        self.assertEqual(len(back.actions), 1)

    def test_the_old_single_action_spelling_is_gone_not_silent(self):
        # `mapping.params` used to be a dict. Answering None would be the one
        # wrong answer worth guarding: it reads as "no parameters".
        mapping = midi_map.Mapping(kind="note", number=41, action="state",
                                   params={"name": "tv_static"})
        self.assertEqual(mapping.actions[0].params, {"name": "tv_static"})
        with self.assertRaises(AttributeError):
            mapping.params
        with self.assertRaises(AttributeError):
            mapping.action


class TheLaunchpad(unittest.TestCase):
    """The lamp protocol, byte for byte against the manual.

    Every one of these runs with no controller plugged in, which is the point
    of keeping the protocol here rather than in the executable: the device is
    the part you cannot rely on having in front of you.
    """
    def test_programmer_mode_is_the_manual_s_bytes(self):
        # F0 00 20 29 02 0C 0E <mode> F7, mode 1 for Programmer, 0 for Live.
        self.assertEqual(launchpad.programmer_mode(True),
                         [0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7])
        self.assertEqual(launchpad.programmer_mode(False),
                         [0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x00, 0xF7])

    def test_the_manuals_own_example_message(self):
        # Straight off the page: "sets up the bottom left pad to static
        # yellow, the pad next to it to flashing green, and the pad next to
        # that pulsing turquoise". If the byte layout here is wrong, this is
        # what catches it.
        message = launchpad._bulk([
            (launchpad.STATIC, 11, (13,)),
            (launchpad.FLASHING, 12, (21, 23)),
            (launchpad.PULSING, 13, (37,)),
        ])
        self.assertEqual(len(message), 1)
        self.assertEqual(
            launchpad.as_hex(message[0]),
            "F0 00 20 29 02 0C 03 00 0B 0D 01 0C 15 17 02 0D 25 F7")

    def test_the_grid_is_row_times_ten_plus_column(self):
        self.assertEqual(launchpad.pad(1, 1), 11)    # bottom left
        self.assertEqual(launchpad.pad(8, 8), 88)    # top right
        for bad in [(0, 1), (9, 1), (1, 0), (1, 9)]:
            with self.assertRaises(ValueError):
                launchpad.pad(*bad)

    def test_an_index_with_no_lamp_is_dropped(self):
        # A mapping learned in another layout can carry a number this surface
        # has nowhere to put. The device answers one with silence, so sending
        # it would only make a real fault harder to see.
        self.assertFalse(launchpad.is_lightable(0))
        self.assertFalse(launchpad.is_lightable(10))     # column 0
        self.assertFalse(launchpad.is_lightable(90))     # row 9, not a pad
        self.assertTrue(launchpad.is_lightable(99))      # the logo is
        self.assertEqual(launchpad.light([(200, 5)]), [])

    def test_a_full_surface_is_one_message(self):
        # 81 is the manual's cap, and the whole surface is exactly 81 lamps -
        # so a full repaint must not split, or the device sees two messages
        # and the seam shows.
        self.assertEqual(len(launchpad.ALL_LEDS), 81)
        messages = launchpad.light([(index, 5) for index in launchpad.ALL_LEDS])
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0][0], 0xF0)
        self.assertEqual(messages[0][-1], 0xF7)

    def test_more_than_the_cap_splits(self):
        specs = [(index, 5) for index in launchpad.ALL_LEDS] * 2
        self.assertEqual(len(launchpad.light(specs)), 2)


class _FakeRig:
    """Just the two things a check asks about."""

    def __init__(self, pattern="mythos26", state=""):
        self.current_pattern = pattern
        self.current_state = state


class TheActionChecks(unittest.TestCase):
    """`check` on an ActionSpec: is this action's effect already so?

    The generic half of a lit surface. Nothing here is about lamps - an
    action answers for itself, the votes are counted in Mapping.is_live, and
    the only thing that cares *why* is whatever is drawing the picture.
    """
    def _at(self, pattern="mythos26", state="", syn=None):
        return midi_map.ActionContext(show=_FakeRig(pattern, state), syn=syn)

    def _cue(self, *actions, **kw):
        return midi_map.Mapping(kind="note", number=41,
                                actions=[midi_map.Action(k, p) for k, p in actions], **kw)

    # -- the two that can answer -------------------------------------------

    def test_a_state_answers_for_itself(self):
        spec = midi_map.ACTIONS["state"]
        self.assertIs(spec.check(self._at(state="tv_static"), {"name": "tv_static"}), True)
        self.assertIs(spec.check(self._at(state="beat_pulse"), {"name": "tv_static"}), False)

    def test_a_pattern_answers_which_machine_is_loaded(self):
        spec = midi_map.ACTIONS["pattern"]
        self.assertIs(spec.check(self._at(pattern="jacket"), {"name": "jacket"}), True)
        self.assertIs(spec.check(self._at(pattern="mythos26"), {"name": "jacket"}), False)

    def test_no_state_at_all_is_a_no_not_a_shrug(self):
        # A plain pattern is running. Whatever this pad sets, the rig is
        # certainly not in it - which is knowledge, not an absence of it.
        spec = midi_map.ACTIONS["state"]
        self.assertIs(spec.check(self._at(pattern="rainbow", state=""),
                                 {"name": "tv_static"}), False)

    def test_the_actions_that_cannot_answer_do_not_try(self):
        # A protocol line, a knob, a tempo: nothing about any of them says
        # "still in force", and syn_control is a fader rather than a cue.
        for key in ("command", "param", "master", "bpm", "syn_control"):
            self.assertIsNone(midi_map.ACTIONS[key].check, key)

    def test_the_synesthesia_cues_do_answer(self):
        for key in ("syn_scene", "syn_preset", "syn_favslot", "syn_media"):
            self.assertIsNotNone(midi_map.ACTIONS[key].check, key)

    # -- how the votes are counted ------------------------------------------

    def test_every_action_that_can_answer_has_to_agree(self):
        # "jacket / campfire" is not live on jacket alone - that is what makes
        # it a cue rather than two buttons.
        cue = self._cue(("pattern", {"name": "jacket"}), ("state", {"name": "campfire"}))

        self.assertIs(cue.is_live(self._at("jacket", "campfire")), True)
        self.assertIs(cue.is_live(self._at("jacket", "digital_void")), False)
        self.assertIs(cue.is_live(self._at("mythos26", "campfire")), False)

    def test_an_action_that_cannot_answer_does_not_veto(self):
        # The common cue: a scene beside a state. If the scene counted as no,
        # the one thing this exists for could never light.
        cue = self._cue(("syn_scene", {"scene": "Neon Grid", "preset": ""}),
                        ("state", {"name": "tv_static"}))

        self.assertIs(cue.is_live(self._at(state="tv_static")), True)
        self.assertIs(cue.is_live(self._at(state="beat_pulse")), False)

    def test_a_synesthesia_row_passes_until_something_says_otherwise(self):
        # With nothing sent and the app's OSC output off there is no way to
        # tell, and the answer that keeps a cue lighting is yes. A scene
        # binding must never be the reason a pad fails to light.
        scene = self._cue(("syn_scene", {"scene": "Neon Grid", "preset": ""}))
        self.assertIs(scene.is_live(self._at(state="tv_static")), True)

    def test_a_row_with_no_actions_that_answer_is_still_none(self):
        # `command` cannot answer and nothing else is in the row.
        row = self._cue(("command", {"line": "bpm 128"}))
        self.assertIsNone(row.is_live(self._at()))

    def test_an_unknown_action_abstains(self):
        row = self._cue(("warp_core", {}), ("state", {"name": "tv_static"}))
        self.assertIs(row.is_live(self._at(state="tv_static")), True)

    def test_a_check_that_throws_costs_that_pad_not_the_repaint(self):
        exploding = midi_map.ActionSpec(
            key="explodes", label="explodes", fields=[],
            run=lambda c, p, v: None,
            check=lambda c, p: (_ for _ in ()).throw(RuntimeError("boom")))
        midi_map.register_action(exploding)
        try:
            row = self._cue(("explodes", {}), ("state", {"name": "tv_static"}))
            self.assertIs(row.is_live(self._at(state="tv_static")), True)
        finally:
            del midi_map.ACTIONS["explodes"]

    def test_no_show_is_no_opinion(self):
        # A desk with no rig attached: the question cannot be asked.
        cue = self._cue(("state", {"name": "tv_static"}))
        self.assertIsNone(cue.is_live(midi_map.ActionContext(show=None)))


class ThePages(unittest.TestCase):
    """Tabs: one machine's looks at a time, so sixty-six fit on sixty-four.

    A page governs firing as well as lighting, and the tests that matter are
    the ones about that: two pages put different rows on the same pad, and a
    tab has to be a way of choosing between them rather than stacking them.
    """
    def _rig(self):
        return _RecorderRig()

    def _ctx(self, rig=None, page=""):
        context = midi_map.ActionContext(show=rig if rig is not None else self._rig())
        context.page = page
        return context

    def _paged(self):
        return midi_map.MappingSet([
            midi_map.Mapping(label="jacket look", kind="note", number=11, page="jacket",
                             action="state", params={"name": "campfire"}),
            midi_map.Mapping(label="scanner look", kind="note", number=11, page="scanner",
                             action="state", params={"name": "boot"}),
            midi_map.Mapping(label="always", kind="note", number=12,
                             action="state", params={"name": "everywhere"}),
        ])

    def test_a_row_with_no_page_is_on_every_page(self):
        row = midi_map.Mapping()
        self.assertTrue(row.on_page(""))
        self.assertTrue(row.on_page("scanner"))

    def test_a_paged_row_is_only_on_its_own(self):
        row = midi_map.Mapping(page="scanner")
        self.assertTrue(row.on_page("scanner"))
        self.assertFalse(row.on_page("jacket"))
        self.assertFalse(row.on_page(""))

    def test_only_the_showing_page_fires(self):
        rig = self._rig()
        context = self._ctx(rig, page="jacket")
        dispatcher = midi_map.Dispatcher(self._paged(), context)

        dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 11 100"))

        self.assertEqual(rig.states, ["campfire"], "the scanner row must not fire")

    def test_the_same_pad_means_something_else_on_another_page(self):
        rig = self._rig()
        context = self._ctx(rig, page="scanner")
        dispatcher = midi_map.Dispatcher(self._paged(), context)

        dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 11 100"))

        self.assertEqual(rig.states, ["boot"])

    def test_a_pageless_row_fires_from_anywhere(self):
        rig = self._rig()
        dispatcher = midi_map.Dispatcher(self._paged(), self._ctx(rig, page="scanner"))
        dispatcher.handle(midi_map.parse_midi_line("ch=1 note_on 12 100"))
        self.assertEqual(rig.states, ["everywhere"])

    def test_an_off_page_pad_is_dark_as_well_as_dead(self):
        # A pad that cannot be pressed must not be lit, or the surface is
        # advertising bindings that do nothing.
        painter = launchpad.LampPainter(self._paged())

        surface = painter.wanted(self._ctx(page="jacket"))

        self.assertIn(11, surface)
        self.assertIn(12, surface)
        self.assertEqual(len(surface), 2, "only one of the two page-11 rows")

    def test_a_pad_the_new_page_does_not_use_goes_dark(self):
        # The failure this guards: jacket's extra pad left lit under scanner,
        # claiming a binding that is no longer reachable.
        mappings = midi_map.MappingSet([
            midi_map.Mapping(label="jacket 11", kind="note", number=11, page="jacket",
                             action="state", params={"name": "campfire"}),
            midi_map.Mapping(label="jacket 13", kind="note", number=13, page="jacket",
                             action="state", params={"name": "parrot"}),
            midi_map.Mapping(label="scanner 11", kind="note", number=11, page="scanner",
                             action="state", params={"name": "boot"}),
        ])
        painter = launchpad.LampPainter(mappings)
        context = self._ctx(page="jacket")
        painter.frame(context)
        self.assertEqual(sorted(painter.wanted(context)), [11, 13])

        context.page = "scanner"
        messages = painter.frame(context)

        self.assertEqual(sorted(painter.wanted(context)), [11])
        self.assertEqual(len(messages), 1)
        # static, pad 13, colour 0 - the only change on the wire
        self.assertEqual(messages[0][7:10],
                         [launchpad.STATIC, 13, launchpad.Colour.OFF])

    def test_an_identical_picture_across_a_page_change_sends_nothing(self):
        # Two pages whose pads happen to look the same: the surface is already
        # right, and the wire is shared with the beat.
        painter = launchpad.LampPainter(self._paged())
        context = self._ctx(page="jacket")
        painter.frame(context)

        context.page = "scanner"
        self.assertEqual(painter.frame(context), [])

    def test_the_page_action_moves_the_surface_and_says_so(self):
        context = self._ctx(page="mythos26")
        spec = midi_map.ACTIONS["page"]

        self.assertEqual(spec.run(context, {"name": "scanner"}, 1.0), "page scanner")
        self.assertEqual(context.page, "scanner")
        # pressed again it is already there and says nothing
        self.assertIsNone(spec.run(context, {"name": "scanner"}, 1.0))

    def test_a_tab_lights_when_its_page_is_showing(self):
        context = self._ctx(page="scanner")
        spec = midi_map.ACTIONS["page"]
        self.assertIs(spec.check(context, {"name": "scanner"}), True)
        self.assertIs(spec.check(context, {"name": "jacket"}), False)

    def test_a_tab_is_live_only_when_both_halves_are(self):
        # A tab changes the surface and the rig. Either alone is a lie: a page
        # whose looks the rig cannot take, or a machine not on the surface.
        tab = midi_map.Mapping(kind="cc", number=89, actions=[
            midi_map.Action("page", {"name": "jacket"}),
            midi_map.Action("pattern", {"name": "jacket"})])

        rig = _FakeRig(pattern="jacket")
        both = midi_map.ActionContext(show=rig); both.page = "jacket"
        self.assertIs(tab.is_live(both), True)

        half = midi_map.ActionContext(show=_FakeRig(pattern="mythos26"))
        half.page = "jacket"
        self.assertIs(tab.is_live(half), False)

    def test_the_file_remembers_which_tab_it_opens_on(self):
        ms = midi_map.MappingSet(self._paged().mappings, opens_on="scanner")
        back = midi_map.MappingSet.from_dict(json.loads(json.dumps(ms.to_dict())))
        self.assertEqual(back.opens_on, "scanner")
        self.assertEqual(back.pages(), ["jacket", "scanner"])

    def test_a_map_written_before_pages_behaves_as_it_did(self):
        # Every row pageless, so every row is on the empty page the desk opens
        # with - which is what a map with no tabs has always done.
        back = midi_map.MappingSet.from_dict(
            {"version": 1, "mappings": [
                {"trigger": {"kind": "note", "number": 36},
                 "action": "state", "params": {"name": "campfire"}}]})
        self.assertEqual(back.opens_on, "")
        self.assertEqual(back.pages(), [])
        self.assertTrue(back.mappings[0].on_page(""))


class TheMomentaryInput(unittest.TestCase):
    """The two inputs a look reads - a jacket's remote buttons, on a rig that
    has none. Bound in value mode, which is what makes an arrow a hold."""

    def _ctx(self):
        rig = _RecorderRig()
        rig.inputs = {"a": False, "b": False}
        rig.set_input = lambda ch, down: rig.inputs.__setitem__(ch, down)
        return midi_map.ActionContext(show=rig), rig

    def test_an_arrow_held_is_the_input_held(self):
        context, rig = self._ctx()
        row = midi_map.Mapping(kind="cc", number=91, mode="value",
                               action="input", params={"channel": "a"})
        dispatcher = midi_map.Dispatcher(midi_map.MappingSet([row]), context)

        dispatcher.handle(midi_map.parse_midi_line("ch=1 cc 91 127"))
        self.assertTrue(rig.inputs["a"])

        dispatcher.handle(midi_map.parse_midi_line("ch=1 cc 91 0"))
        self.assertFalse(rig.inputs["a"], "the release has to let go")

    def test_the_lamp_follows_the_finger(self):
        context, rig = self._ctx()
        spec = midi_map.ACTIONS["input"]

        self.assertIs(spec.check(context, {"channel": "a"}), False)
        spec.run(context, {"channel": "a"}, 1.0)
        self.assertIs(spec.check(context, {"channel": "a"}), True)
        self.assertIs(spec.check(context, {"channel": "b"}), False)

    def test_only_a_and_b_exist(self):
        context, _rig = self._ctx()
        said = midi_map.ACTIONS["input"].run(context, {"channel": "c"}, 1.0)
        self.assertIn("a or b", said)

    def test_it_says_nothing_on_either_edge(self):
        # Both edges of every press would bury the header.
        context, _rig = self._ctx()
        spec = midi_map.ACTIONS["input"]
        self.assertIsNone(spec.run(context, {"channel": "a"}, 1.0))
        self.assertIsNone(spec.run(context, {"channel": "a"}, 0.0))


class TheSynesthesiaState(unittest.TestCase):
    """What the visualiser is doing, and how sure we are of it.

    Two sources that do not rank equally: what this desk asked for, which is
    always available, and what the app announced, which is true.
    """
    def _at(self, syn):
        return midi_map.ActionContext(show=None, syn=syn)

    def test_nothing_known_is_a_yes(self):
        # OSC output off and no cue fired yet. There is no way to tell, and a
        # Synesthesia binding must never be the reason a pad fails to light.
        syn = midi_map.SynesthesiaState()
        self.assertIs(syn.scene_is("Neon Grid"), True)

    def test_what_was_sent_is_believed_until_the_app_speaks(self):
        syn = midi_map.SynesthesiaState()
        syn.scene_sent("Neon Grid")
        self.assertIs(syn.scene_is("Neon Grid"), True)
        self.assertIs(syn.scene_is("Hex Array"), False)

    def test_what_the_app_said_outranks_what_we_sent(self):
        # The case the sent record gets wrong: a scene changed in
        # Synesthesia's own window, which nothing else here can see.
        syn = midi_map.SynesthesiaState()
        syn.scene_sent("Neon Grid")
        syn.hear("/scenes/hexarray")

        self.assertIs(syn.scene_is("Neon Grid"), False)
        self.assertIs(syn.scene_is("Hex Array"), True)

    def test_the_scene_name_is_folded_the_way_the_app_spells_it(self):
        # "Neon Grid" is /scenes/neongrid - lowercase, spaces and hyphens
        # gone. A map carries the human spelling; the wire carries neither.
        syn = midi_map.SynesthesiaState()
        syn.hear("/scenes/neongrid")
        for spelling in ("Neon Grid", "neon grid", "Neon-Grid", "neon_grid"):
            self.assertIs(syn.scene_is(spelling), True, spelling)

    def test_only_scene_addresses_are_heard(self):
        # Forty audio uniforms a frame arrive on the same port.
        syn = midi_map.SynesthesiaState()
        for noise in ("/syn_BassLevel", "/controls/global/color/1", "/audio/level"):
            self.assertFalse(syn.hear(noise), noise)
        self.assertEqual(syn.heard_scene, "")
        self.assertTrue(syn.hear("/scenes/neongrid"))

    def test_a_new_scene_retires_the_preset(self):
        # A preset belongs to the scene that was running when it loaded, and
        # claiming it across a scene change would light a pad for something
        # no longer on screen.
        syn = midi_map.SynesthesiaState()
        syn.sent_preset = "Deep"
        syn.scene_sent("Hex Array")
        self.assertEqual(syn.sent_preset, "")

    # -- through the actions ------------------------------------------------

    def test_firing_a_scene_records_it(self):
        link = _RecorderLink()
        syn = midi_map.SynesthesiaState()
        context = midi_map.ActionContext(osc_factory=lambda: link, syn=syn)
        spec = midi_map.ACTIONS["syn_scene"]

        spec.run(context, {"scene": "Neon Grid", "preset": "Deep"}, 1.0)

        self.assertEqual(syn.sent_scene, "Neon Grid")
        self.assertEqual(syn.sent_preset, "Deep")
        self.assertIs(spec.check(context, {"scene": "Neon Grid"}), True)
        self.assertIs(spec.check(context, {"scene": "Hex Array"}), False)

    def test_a_favslot_pad_lights_the_one_last_fired(self):
        link = _RecorderLink()
        syn = midi_map.SynesthesiaState()
        context = midi_map.ActionContext(osc_factory=lambda: link, syn=syn)
        spec = midi_map.ACTIONS["syn_favslot"]

        # Nothing fired yet: no way to tell, so yes.
        self.assertIs(spec.check(context, {"slot": 3}), True)

        spec.run(context, {"slot": 3}, 1.0)
        self.assertIs(spec.check(context, {"slot": 3}), True)
        self.assertIs(spec.check(context, {"slot": 4}), False)

    def test_the_osc_input_records_a_scene_the_app_announced(self):
        # The wiring that makes the lamp true rather than hopeful: the
        # announcement arrives on the audio port, through the OSC dispatcher.
        syn = midi_map.SynesthesiaState()
        dispatcher = osc_input.Dispatcher(
            osc_input.BindingSet(),
            midi_map.ActionContext(show=None, syn=syn))

        dispatcher.handle("/syn_BassLevel", [0.4])
        self.assertEqual(syn.heard_scene, "")

        dispatcher.handle("/scenes/neongrid", ["Neon Grid"])
        self.assertEqual(syn.heard_scene, "/scenes/neongrid")
        self.assertIs(syn.scene_is("Neon Grid"), True)


class TheLampPainter(unittest.TestCase):
    """Which pads are lit, and which one is live."""

    def _map(self, *rows):
        return midi_map.MappingSet(list(rows))

    def _at(self, pattern="mythos26", state="", syn=None):
        """The context a check is asked against."""
        return midi_map.ActionContext(show=_FakeRig(pattern, state), syn=syn)

    def _cue(self, number, state, colour=41, **kw):
        return midi_map.Mapping(label=state, kind="note", number=number,
                                colour=colour, action="state",
                                params={"name": state}, **kw)

    def test_a_bound_pad_is_lit_and_the_live_one_pulses(self):
        painter = launchpad.LampPainter(
            self._map(self._cue(11, "tv_static"), self._cue(12, "beat_pulse")))

        surface = painter.wanted(self._at(state="tv_static"))

        self.assertEqual(surface[11], (launchpad.PULSING, 41))
        self.assertEqual(surface[12], (launchpad.STATIC, 41))

    def test_the_live_pad_follows_the_rig_not_the_pad_that_was_pressed(self):
        # The whole reason this reads current_state rather than remembering
        # the last press: a state changed from the desk or from OSC has to
        # move the lamp too.
        painter = launchpad.LampPainter(
            self._map(self._cue(11, "tv_static"), self._cue(12, "beat_pulse")))

        self.assertEqual(painter.wanted(self._at(state="beat_pulse"))[12][0], launchpad.PULSING)
        self.assertEqual(painter.wanted(self._at(state="beat_pulse"))[11][0], launchpad.STATIC)

    def test_a_scene_pad_pulses_for_the_scene_the_app_is_on(self):
        painter = launchpad.LampPainter(self._map(
            midi_map.Mapping(kind="note", number=11, colour=53,
                             action="syn_scene", params={"scene": "Neon Grid"}),
            midi_map.Mapping(kind="note", number=12, colour=53,
                             action="syn_scene", params={"scene": "Hex Array"})))

        syn = midi_map.SynesthesiaState()
        syn.hear("/scenes/neongrid")          # the app announced it
        surface = painter.wanted(self._at(syn=syn))

        self.assertEqual(surface[11][0], launchpad.PULSING)
        self.assertEqual(surface[12][0], launchpad.STATIC)

    def test_a_disabled_mapping_is_dark(self):
        painter = launchpad.LampPainter(
            self._map(self._cue(11, "tv_static", enabled=False)))
        self.assertEqual(painter.wanted(self._at(state="tv_static")), {})

    def test_a_pad_off_this_surface_is_skipped(self):
        painter = launchpad.LampPainter(self._map(self._cue(60, "tv_static")))
        self.assertEqual(painter.wanted(self._at(state="tv_static")), {})

    def test_repainting_the_same_picture_sends_nothing(self):
        # The wire is shared with the beat; a repaint every tick would not be.
        painter = launchpad.LampPainter(
            self._map(self._cue(11, "tv_static"), self._cue(12, "beat_pulse")))

        self.assertTrue(painter.frame(self._at(state="tv_static")))
        self.assertEqual(painter.frame(self._at(state="tv_static")), [])
        self.assertEqual(painter.frame(self._at(state="tv_static")), [])

    def test_only_the_pads_that_changed_are_sent(self):
        mappings = self._map(self._cue(11, "tv_static"), self._cue(12, "beat_pulse"),
                             self._cue(13, "tv_static_mono"))
        painter = launchpad.LampPainter(mappings)
        painter.frame(self._at(state="tv_static"))

        messages = painter.frame(self._at(state="beat_pulse"))

        self.assertEqual(len(messages), 1)
        # header + command + two specs of three bytes + F7
        self.assertEqual(len(messages[0]), 6 + 1 + (2 * 3) + 1)

    def test_a_pad_that_lost_its_binding_goes_out(self):
        mappings = self._map(self._cue(11, "tv_static"), self._cue(12, "beat_pulse"))
        painter = launchpad.LampPainter(mappings)
        painter.frame(self._at(state="tv_static"))

        del mappings.mappings[1]
        messages = painter.frame(self._at(state="tv_static"))

        self.assertEqual(len(messages), 1)
        # static, pad 12, colour 0 - the only change
        self.assertEqual(messages[0][7:10], [launchpad.STATIC, 12, launchpad.Colour.OFF])

    def test_forget_makes_the_next_frame_a_full_repaint(self):
        # For after a mode switch, when the device has forgotten what it was
        # showing but the painter has not.
        painter = launchpad.LampPainter(self._map(self._cue(11, "tv_static")))
        painter.frame(self._at(state="tv_static"))
        self.assertEqual(painter.frame(self._at(state="tv_static")), [])

        painter.forget()
        self.assertTrue(painter.frame(self._at(state="tv_static")))

    def test_the_live_pad_wins_when_two_bindings_share_it(self):
        # Old maps did this before actions were a list, and still load.
        painter = launchpad.LampPainter(self._map(
            self._cue(11, "tv_static"),
            midi_map.Mapping(kind="cc", number=11, mode="value", colour=9,
                             action="master", params={"low": 0.0, "high": 1.0})))

        self.assertEqual(painter.wanted(self._at(state="tv_static"))[11][0], launchpad.PULSING)

    def test_a_machine_cue_pulses_only_when_both_halves_are_so(self):
        # The pad that loads a machine and opens one of its looks. Lit on the
        # wrong machine, lit on the right machine at the wrong look, pulsing
        # only when it is actually what the rig is doing.
        painter = launchpad.LampPainter(self._map(
            midi_map.Mapping(kind="note", number=11, colour=41, actions=[
                midi_map.Action("pattern", {"name": "jacket"}),
                midi_map.Action("state", {"name": "campfire"})])))

        self.assertEqual(painter.wanted(self._at("mythos26", "beat_pulse"))[11][0],
                         launchpad.STATIC)
        self.assertEqual(painter.wanted(self._at("jacket", "digital_void"))[11][0],
                         launchpad.STATIC)
        self.assertEqual(painter.wanted(self._at("jacket", "campfire"))[11][0],
                         launchpad.PULSING)

    def test_a_pad_that_only_loads_a_machine_pulses_on_any_of_its_looks(self):
        painter = launchpad.LampPainter(self._map(
            midi_map.Mapping(kind="note", number=12, colour=9,
                             action="pattern", params={"name": "jacket"})))

        self.assertEqual(painter.wanted(self._at("jacket", "campfire"))[12][0],
                         launchpad.PULSING)
        self.assertEqual(painter.wanted(self._at("jacket", "parrot"))[12][0],
                         launchpad.PULSING)
        self.assertEqual(painter.wanted(self._at("mythos26", "beat_pulse"))[12][0],
                         launchpad.STATIC)

    def test_the_colour_is_the_mapping_s_own(self):
        painter = launchpad.LampPainter(self._map(
            self._cue(11, "tv_static", colour=launchpad.Colour.AMBER),
            self._cue(12, "beat_pulse", colour=launchpad.Colour.PINK)))

        surface = painter.wanted(self._at())
        self.assertEqual(surface[11][1], launchpad.Colour.AMBER)
        self.assertEqual(surface[12][1], launchpad.Colour.PINK)


class _RecorderShow:
    """Stands in for ShowController: a fixed set of knobs, and a record of
    what apply_preset sent at them."""

    def __init__(self, param_names, curve_names=()):
        from eclipse_dmx.controller import Param
        self.params = [Param(name, "f", 0.5, 0.0, 1.0) for name in param_names]
        self.curves = {name: [(0.0, 0.0, None), (1.0, 1.0, None)]
                       for name in curve_names}
        self.sent = []

    def get_param(self, name):
        return next((p for p in self.params if p.name == name), None)

    def set_param(self, name, value):
        self.sent.append(("param", name, value))

    def set_curve(self, name, keys):
        self.sent.append(("curve", name, list(keys)))


class TheLookPresets(unittest.TestCase):
    """Keeping a state's knobs as a .config file, and putting them back."""

    def setUp(self):
        import tempfile
        self.dir = Path(tempfile.mkdtemp())

    def test_snapshot_keeps_every_knob_and_curve(self):
        show = _RecorderShow(["speed", "color"], ["envelope"])
        show.params[1].kind = "c"
        show.params[1].value = "#ff8800"
        preset = look_presets.snapshot(show, "warm", "mythos26", "campfire")
        self.assertEqual(preset.params, {"speed": 0.5, "color": "#ff8800"})
        self.assertEqual(list(preset.curves), ["envelope"])

    def test_round_trips_through_the_file(self):
        preset = look_presets.LookPreset(
            name="warm dusk", pattern="mythos26", state="campfire",
            params={"speed": 0.25, "color": "#ff8800"},
            curves={"envelope": [(0.0, 0.0, None), (0.06, 1.0, "EaseOutCubic")]})
        path = look_presets.save_preset(self.dir, preset)
        self.assertTrue(path.name.endswith(".config"))
        back = look_presets.load_preset(path)
        self.assertEqual(back.to_dict(), preset.to_dict())

    def test_listed_under_its_state_by_its_pretty_name(self):
        preset = look_presets.LookPreset(name="warm dusk!", state="campfire")
        look_presets.save_preset(self.dir, preset)
        self.assertEqual(look_presets.list_presets(self.dir, "campfire"),
                         ["warm dusk!"])
        # the filename was folded; the display name was not
        self.assertEqual(look_presets.list_presets(self.dir, "digital_void"), [])

    def test_no_folder_is_no_presets_not_an_error(self):
        self.assertEqual(look_presets.list_presets(self.dir / "nope", "x"), [])

    def test_apply_matches_by_name_and_reports_the_rest(self):
        show = _RecorderShow(["speed"], ["envelope"])
        preset = look_presets.LookPreset(
            name="p", params={"speed": 0.8, "gone": 0.1},
            curves={"envelope": [(0.0, 1.0, None)], "vanished": [(0.0, 0.0, None)]})
        applied, skipped = look_presets.apply_preset(show, preset)
        self.assertEqual(applied, 2)
        self.assertEqual(sorted(skipped), ["gone", "~vanished"])
        self.assertIn(("param", "speed", 0.8), show.sent)
        self.assertIn(("curve", "envelope", [(0.0, 1.0, None)]), show.sent)
        # nothing was sent for the names with no home
        self.assertEqual(len(show.sent), 2)

    def test_a_broken_file_hides_itself_not_the_list(self):
        look_presets.save_preset(self.dir, look_presets.LookPreset(
            name="good", state="campfire"))
        bad = look_presets.state_dir(self.dir, "campfire") / "bad.config"
        bad.write_text("not json", encoding="utf-8")
        self.assertEqual(look_presets.list_presets(self.dir, "campfire"), ["good"])


if __name__ == "__main__":
    unittest.main()
