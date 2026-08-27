"""Tests for the python wrapper and the viewer.

Plain unittest, so this needs nothing installed:

    cd desktop
    python -m unittest discover -s python/tests -v

Anything that needs the executable or a display skips itself when there is not
one, so this stays runnable on a build machine and on a show laptop.
"""

from __future__ import annotations

import io
import json
import sys
import time
import unittest
from pathlib import Path

DESKTOP = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DESKTOP / "python"))

from eclipse_dmx.binary import BinaryNotFoundError, find_executable  # noqa: E402
from eclipse_dmx.config import (  # noqa: E402
    MYTHOS26_STATES,
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
from eclipse_dmx import osc  # noqa: E402

RIG = DESKTOP / "config" / "uking_par36_x10.json"
SHOW = DESKTOP / "config" / "mythos26.json"
OBELISK = DESKTOP / "config" / "obelisk.json"
OBELISK_USB = DESKTOP / "config" / "obelisk_usb.json"
SCANNER = DESKTOP / "config" / "scanner.json"

#: The sculpture's own numbers, from src/relics/obelisk/state_obelisk.h and the
#: eight GenerateAxisRow calls in obelisk.cpp. The config has to agree with
#: these or a look tuned on the desk is tuned on the wrong shape.
OBELISK_SIDE_LENGTH = 43
OBELISK_STRIPS = 8
OBELISK_PIXELS = OBELISK_SIDE_LENGTH * OBELISK_STRIPS


def executable_or_skip():
    try:
        return find_executable()
    except BinaryNotFoundError as error:
        raise unittest.SkipTest(f"eclipse-dmx is not built: {error}")


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


class OscColour(unittest.TestCase):
    """What actually goes out for a given frame byte."""

    def setUp(self):
        self.sent = []

    def link(self, **kwargs):
        made = osc.SynesthesiaLink("127.0.0.1:1", **kwargs)
        made._send = self.sent.append       # no socket involved
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


class TheLinkProtocol(unittest.TestCase):
    """The wire format and the relic's end of it, exercised by the executable.

    `--link-selftest` drives elink and a real ObeliskCore with no hardware:
    resync, checksums, the takeover and the handback. It is the firmware's own
    code, so this is the closest thing to testing the sculpture that exists
    without flashing one.
    """

    def test_selftest_passes(self):
        import subprocess

        executable = executable_or_skip()
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
        import subprocess

        executable = executable_or_skip()
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
        show = ShowController(RIG, dry_run=True, on_frame=lambda f: None)
        try:
            time.sleep(0.4)
            with self.assertRaises(ShowError):
                show.set_link_mode("cue")
            # And the show survives being asked.
            self.assertTrue(show.is_running)
        finally:
            show.stop()

    def test_a_bad_link_mode_is_caught_before_the_wire(self):
        show = ShowController(RIG, dry_run=True, on_frame=lambda f: None)
        try:
            with self.assertRaises(ShowError):
                show.set_link_mode("sideways")
        finally:
            show.stop()

    def test_probe_does_not_claim_a_dmx_widget(self):
        """The whole point of probing rather than guessing at port names.

        Passes trivially with nothing attached; the case it guards is a machine
        with a widget on it, which is this one.
        """
        import subprocess

        executable = executable_or_skip()
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


class FrameStream(unittest.TestCase):
    """--emit-frames, end to end, with no hardware."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def test_frames_arrive_and_move(self):
        frames = []
        show = ShowController(RIG, dry_run=True, on_frame=frames.append, emit_rate=30.0)
        try:
            show.set_pattern("obelisk_seasons")
            time.sleep(1.5)
        finally:
            show.stop()

        self.assertGreater(len(frames), 20)
        self.assertTrue(all(len(frame) == 10 for frame in frames))
        self.assertGreater(len({tuple(frame) for frame in frames}), 5)

    def test_fixture_names_are_reported(self):
        show = ShowController(RIG, dry_run=True, on_frame=lambda f: None)
        try:
            time.sleep(0.5)
            self.assertEqual(show.fixture_names, [f"par_{i}" for i in range(1, 11)])
        finally:
            show.stop()

    def test_frames_do_not_pile_up_in_events(self):
        """A show left running for an hour must not grow a list per frame."""
        frames = []
        show = ShowController(RIG, dry_run=True, on_frame=frames.append)
        try:
            time.sleep(1.2)
        finally:
            show.stop()

        self.assertGreater(len(frames), 20)
        self.assertLess(len(show.events), 20)

    def test_blackout_reaches_the_wire(self):
        frames = []
        show = ShowController(RIG, dry_run=True, on_frame=frames.append)
        try:
            show.set_pattern("obelisk_mono")
            time.sleep(0.5)
            show.blackout(True)
            time.sleep(0.6)
            self.assertTrue(all(colour == (0, 0, 0) for colour in frames[-1]))

            show.blackout(False)
            time.sleep(0.6)
            self.assertTrue(any(colour != (0, 0, 0) for colour in frames[-1]))
        finally:
            show.stop()

    def test_master_dims_the_wire(self):
        frames = []
        show = ShowController(RIG, dry_run=True, on_frame=frames.append)
        try:
            show.set_pattern("obelisk_mono")
            time.sleep(0.5)
            full = frames[-1][0]
            show.set_master(0.25)
            time.sleep(0.5)
            dim = frames[-1][0]
        finally:
            show.stop()

        self.assertLess(sum(dim), sum(full))

    def test_emit_rate_is_honoured(self):
        """Asking for 20 must give 20, not whatever the render loop rounds to."""
        stamps = []
        show = ShowController(
            RIG, dry_run=True, on_frame=lambda f: stamps.append(time.monotonic()), emit_rate=20.0
        )
        try:
            time.sleep(0.4)
            stamps.clear()
            start = time.monotonic()
            time.sleep(2.0)
            measured = len(stamps) / (time.monotonic() - start)
        finally:
            show.stop()

        self.assertAlmostEqual(measured, 20.0, delta=20.0 * 0.15)


class ObeliskOnTheDesk(unittest.TestCase):
    """The sculpture's own looks, rendered on the sculpture's own shape."""

    @classmethod
    def setUpClass(cls):
        cls.executable = executable_or_skip()

    def _last_frame(self, pattern):
        """One settled frame of `pattern`, as 344 (r, g, b)."""
        frames = []
        show = ShowController(OBELISK, dry_run=True, on_frame=frames.append, emit_rate=20.0)
        try:
            show.set_pattern(pattern)
            time.sleep(1.2)
        finally:
            show.stop()

        self.assertTrue(frames, "no frames arrived")
        return frames[-1]

    def test_preview_runs_with_no_widget_attached(self):
        """Not --dry-run: the config's own output, on a machine with no wire.

        "auto" with nothing plugged in is a startup error for a DMX rig, and
        must not be one for a rig that was never going to touch a wire.
        """
        frames = []
        show = ShowController(OBELISK, dry_run=False, on_frame=frames.append)
        try:
            time.sleep(1.0)
            self.assertTrue(show.is_running)
        finally:
            show.stop()

        self.assertTrue(frames)

    def test_the_whole_strip_reaches_the_frame(self):
        """Including the 173 pixels that sit past channel 512."""
        frame = self._last_frame("obelisk_seasons")
        self.assertEqual(len(frame), OBELISK_PIXELS)
        self.assertTrue(any(colour != (0, 0, 0) for colour in frame[171:]))

    def test_the_patch_matches_what_python_resolved(self):
        """Both sides parse this file, so they can disagree. Catch it."""
        import subprocess

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


class JacketStateMachine(unittest.TestCase):
    """The jacket's looks, driven over the protocol instead of by buttons."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def test_states_are_announced(self):
        show = ShowController(RIG, dry_run=True, on_frame=lambda f: None)
        try:
            show.command("pattern jacket")
            time.sleep(0.5)
            self.assertIn("digital_void", show.state_names)
            self.assertEqual(len(show.state_names), 12)
        finally:
            show.stop()

    def test_config_opens_on_its_chosen_state(self):
        """The rig config names a state; digital_void is nearly black."""
        show = ShowController(RIG, dry_run=True, on_frame=lambda f: None)
        try:
            time.sleep(0.5)
            self.assertEqual(show.current_state, Config.load(RIG).pattern.state)
        finally:
            show.stop()

    def test_switching_state_changes_the_rig(self):
        frames = []
        show = ShowController(RIG, dry_run=True, on_frame=frames.append, emit_rate=20.0)
        try:
            show.command("pattern jacket")
            seen = []
            for state in ("parrot", "blue_magic", "campfire"):
                show.set_state(state)
                time.sleep(1.0)          # past the 0.4s cross-fade
                seen.append(tuple(frames[-1]))
            self.assertEqual(len(set(seen)), 3)
        finally:
            show.stop()

    def test_unknown_state_is_rejected_without_dying(self):
        show = ShowController(RIG, dry_run=True, on_frame=lambda f: None)
        try:
            show.command("pattern jacket")
            time.sleep(0.4)
            with self.assertRaises(ShowError):
                show.set_state("not_a_look")
            self.assertTrue(show.is_running)
        finally:
            show.stop()

    def test_state_needs_a_state_machine(self):
        show = ShowController(RIG, dry_run=True, on_frame=lambda f: None)
        try:
            show.command("pattern obelisk_seasons")
            time.sleep(0.4)
            self.assertEqual(show.state_names, [])
            with self.assertRaises(ShowError):
                show.set_state("campfire")
        finally:
            show.stop()

    def test_input_modulates_the_look(self):
        frames = []
        show = ShowController(RIG, dry_run=True, on_frame=frames.append, emit_rate=20.0)
        try:
            show.command("pattern jacket")
            show.set_state("digital_void")
            time.sleep(1.0)
            before = tuple(frames[-1])
            show.set_input("a", True)
            time.sleep(1.2)
            after = tuple(frames[-1])
            self.assertNotEqual(before, after)
        finally:
            show.stop()


class MidiSettings(unittest.TestCase):
    """The config side of the beat clock. No device needed."""

    def test_the_show_config_loads_clean(self):
        config = Config.load(SHOW)
        self.assertEqual(config.validate(), [])
        self.assertEqual(config.pattern.name, "mythos26")
        self.assertEqual(config.pattern.state, "beat_pulse")
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
        config.pattern.state = "slot_9"
        with self.assertRaises(ConfigError):
            config.validate()


class Mythos26(unittest.TestCase):
    """The show, driven over the protocol."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def _show(self, **kwargs):
        # --midi "" so the test never opens a device that happens to be
        # plugged into the machine running it.
        kwargs.setdefault("midi", "")
        return ShowController(SHOW, dry_run=True, **kwargs)

    def test_seven_states_in_table_order(self):
        show = self._show(on_frame=lambda f: None)
        try:
            time.sleep(0.5)
            self.assertEqual(show.state_names, list(MYTHOS26_STATES))
        finally:
            show.stop()

    def test_the_static_list_matches_the_executable(self):
        show = self._show(on_frame=lambda f: None)
        try:
            time.sleep(0.5)
            self.assertEqual(tuple(show.state_names), MYTHOS26_STATES)
        finally:
            show.stop()

    def test_it_pulses_white_on_the_beat(self):
        """Full white once a beat, well down between, and every fixture together."""
        frames = []
        show = self._show(on_frame=frames.append, bpm=120.0, emit_rate=40.0)
        try:
            time.sleep(2.5)  # five beats at 120
        finally:
            show.stop()

        self.assertTrue(frames, "no frames arrived")

        # White: a hit has r == g == b. A colour cast here would mean the
        # channel order is wrong, which is the one thing white catches.
        peaks = [f for f in frames if max(f[0]) > 200]
        self.assertTrue(peaks, "the rig never reached full brightness")
        for frame in peaks:
            for red, green, blue in frame:
                self.assertEqual((red, green, blue), (red, red, red))

        # The whole rig hits together.
        for frame in peaks:
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
            show = self._show(on_frame=frames.append, bpm=bpm, emit_rate=40.0)
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
        try:
            time.sleep(1.5)
            self.assertAlmostEqual(show.bpm, 90.0, delta=1.0)
            self.assertFalse(show.beat_locked, "nothing external is driving it")
            self.assertIn("bpm=90", show.status())
        finally:
            show.stop()

    def test_tapping_a_beat_relights_the_rig(self):
        frames = []
        show = self._show(on_frame=frames.append, bpm=40.0, emit_rate=40.0)
        try:
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
        finally:
            show.stop()

    def test_a_bad_tempo_is_rejected_without_dying(self):
        show = self._show(on_frame=lambda f: None)
        try:
            with self.assertRaises(ShowError):
                show.set_bpm(5.0)
            self.assertTrue(show.is_running)
        finally:
            show.stop()

    def test_beat_lines_do_not_pile_up_in_events(self):
        """Same reason frame lines do not: a show runs for hours."""
        show = self._show(on_frame=lambda f: None, bpm=240.0)
        try:
            time.sleep(1.5)
            self.assertFalse([e for e in show.events if e.startswith("BEAT")])
            self.assertGreater(show.beat, 0)
        finally:
            show.stop()

    def test_free_run_off_stops_the_pulse(self):
        """With nothing driving it and free-run off, the rig should settle dark."""
        frames = []
        show = self._show(on_frame=frames.append, bpm=120.0, emit_rate=40.0)
        try:
            show.set_free_run(False)
            time.sleep(1.5)
            self.assertEqual(max(frames[-1][0]), 0)
        finally:
            show.stop()

    def test_the_placeholders_are_visible_and_distinct(self):
        frames = []
        show = self._show(on_frame=frames.append, emit_rate=20.0)
        try:
            seen = []
            for state in ("slot_5", "slot_6", "slot_7"):
                show.set_state(state)
                time.sleep(0.8)  # past the 0.25s cross-fade
                seen.append(tuple(frames[-1]))

            for frame in seen:
                self.assertGreater(max(max(f) for f in frame), 0, "a slot rendered black")
            self.assertEqual(len(set(seen)), 3, "two slots look the same")
        finally:
            show.stop()


def _rising_edges(frames, threshold=128):
    """How many times the rig went from dark to lit.

    Counting edges rather than lit frames: a pulse spans several frames and how
    many depends on the frame rate we happened to get.
    """
    lit = [max(f[0]) > threshold for f in frames]
    return sum(1 for i in range(1, len(lit)) if lit[i] and not lit[i - 1])


class BeatLooks(unittest.TestCase):
    """The looks that fire on the beat: their envelope, and their rate.

    There used to be a divider here - on 1 / on 2 / on 4. It went, because the
    clock counts beats and has no idea which of them is the one, so "on 4"
    fired at the right rate on an arbitrary beat of the bar with no usable way
    to move it. Half time is back as a rate because a pair *has* a usable way:
    setting the rate, or entering the cue, seats it on the beat you did that
    on. Double time never had the problem - it lands on the beat and between
    them, whichever beat it counts from.
    """

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def _count(self, state, seconds=4.0, bpm=120.0, params=None):
        frames = []
        show = ShowController(
            SHOW, dry_run=True, midi="", bpm=bpm, on_frame=frames.append, emit_rate=40.0
        )
        try:
            show.set_state(state)
            for name, value in (params or {}).items():
                show.set_param(name, value)
            frames.clear()          # drop the cross-fade
            time.sleep(seconds)
        finally:
            show.stop()
        return _rising_edges(frames)

    def test_beat_pulse_fires_on_every_beat(self):
        beats = 120.0 / 60.0 * 4.0
        self.assertAlmostEqual(self._count("beat_pulse"), beats, delta=2.0)

    def test_vu_pulse_fires_on_every_beat_too(self):
        """It used to open on twos. Without a divider there is one answer."""
        beats = 120.0 / 60.0 * 4.0
        self.assertAlmostEqual(self._count("vu_pulse"), beats, delta=2.0)

    def test_the_cue_list_sets_the_envelope(self):
        """beatLook's two numbers are what the look opens on."""
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None)
        try:
            show.set_state("beat_pulse")
            time.sleep(0.4)
            knobs = {p.name: p.value for p in show.params}
            self.assertAlmostEqual(knobs["attack"], 0.15, places=3)
            self.assertAlmostEqual(knobs["decay"], 0.60, places=3)

            # vu_pulse opens shorter and sharper, over its lit wash.
            show.set_state("vu_pulse")
            time.sleep(0.4)
            knobs = {p.name: p.value for p in show.params}
            self.assertAlmostEqual(knobs["attack"], 0.10, places=3)
            self.assertAlmostEqual(knobs["decay"], 0.45, places=3)
        finally:
            show.stop()

    def test_the_envelope_is_still_live(self):
        """Stated in the cue list, tunable at the desk - both, not either."""
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None)
        try:
            show.set_state("beat_pulse")
            time.sleep(0.4)
            show.set_param("decay", 1.25)
            time.sleep(0.3)
            knobs = {p.name: p.value for p in show.params}
            self.assertAlmostEqual(knobs["decay"], 1.25, places=3)
        finally:
            show.stop()

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

        Tapped rather than free-run because that is the path that broke it. A
        tap goes through `BeatClock::markBeat`, which guarantees the beat number
        moves forward but not that it moves by one: a beat landing a hair off
        the predicted one advances it by two, and half time derived from that
        number changes which beat of the pair it is on every time that happens.
        Python's timing jitter is the same hair, so the taps below reproduce it
        without needing Mixxx on the other end of a cable.
        """
        period = 0.35
        stamped = []
        show = ShowController(SHOW, dry_run=True, midi="",
                              on_frame=lambda frame: stamped.append(
                                  (time.monotonic(), max(frame[0]) > 128)),
                              emit_rate=40.0)
        try:
            show.set_state("beat_pulse")
            show.set_param("rate", 0.5)
            show.set_param("decay", 0.12)   # a hit that ends inside one beat
            show.set_param("attack", 0.02)

            for _ in range(16):
                show.command("beat")
                time.sleep(period)
            stamped.clear()

            for _ in range(16):
                show.command("beat")
                time.sleep(period)
        finally:
            show.stop()

        edges = [now for index, (now, lit) in enumerate(stamped)
                 if lit and index > 0 and not stamped[index - 1][1]]
        self.assertGreaterEqual(len(edges), 4, "too few hits to judge the spacing")

        gaps = [b - a for a, b in zip(edges, edges[1:])]
        for gap in gaps:
            self.assertAlmostEqual(gap, period * 2, delta=period * 0.5,
                                   msg=f"hits {period * 2:.2f}s apart expected, got {gaps}")

    def test_the_cue_list_sets_the_rate(self):
        """Both looks open on the beat; the rate is a live knob from there."""
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None)
        try:
            for state in ("beat_pulse", "vu_pulse"):
                show.set_state(state)
                time.sleep(0.4)
                knobs = {p.name: p.value for p in show.params}
                self.assertAlmostEqual(knobs["rate"], 1.0, places=3, msg=state)
        finally:
            show.stop()

    def test_the_rate_snaps_to_the_musical_ones(self):
        """A slider will hand over 1.37. Nobody wants 1.37 hits a beat."""
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None)
        try:
            show.set_state("beat_pulse")
            time.sleep(0.4)
            for sent, landed in ((0.5, 0.5), (0.7, 0.5), (0.9, 1.0),
                                 (1.37, 1.0), (1.6, 2.0), (2.0, 2.0)):
                show.set_param("rate", sent)
                self.assertAlmostEqual(show.get_param("rate").value, landed, places=3,
                                       msg=f"sent {sent}")
        finally:
            show.stop()

    def test_beat_div_is_gone(self):
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None)
        try:
            with self.assertRaises(ShowError):
                show.command("beat div 2")
            self.assertTrue(show.is_running)
        finally:
            show.stop()


class TvStatic(unittest.TestCase):
    """Every fixture a new value every frame."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def _frames(self, state, seconds=1.5):
        frames = []
        show = ShowController(
            SHOW, dry_run=True, midi="", on_frame=frames.append, emit_rate=40.0
        )
        try:
            show.set_state(state)
            time.sleep(0.5)     # past the cross-fade
            frames.clear()
            time.sleep(seconds)
        finally:
            show.stop()
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
        import subprocess

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
        import subprocess

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

        import subprocess

        result = subprocess.run(
            [str(find_executable()), "--config", str(SHOW), "--dry-run",
             "--frames", "2", "--no-stdin"],
            capture_output=True, text=True, timeout=30,
        )

        # Only meaningful on a machine whose only MIDI input is the controller,
        # which is the machine this was written on. Elsewhere, auto may well
        # find something legitimate, and that is not a failure.
        if "every MIDI input is on midi.ignore" in result.stdout:
            self.assertIn("READY", result.stdout, "it should still light up")
            self.assertNotIn("MIDI-OPEN", result.stdout)


class MidiDiscovery(unittest.TestCase):
    """Enumeration only - there is no guarantee of a device on any machine."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def test_listing_midi_inputs_does_not_fail(self):
        from eclipse_dmx.ports import list_midi_ports

        ports = list_midi_ports()
        self.assertIsInstance(ports, list)
        for port in ports:
            self.assertIsInstance(port.index, int)

    def test_opening_a_port_that_is_not_there_is_an_error_not_a_crash(self):
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None)
        try:
            with self.assertRaises(ShowError):
                show.midi_open("definitely-not-a-midi-port")
            self.assertTrue(show.is_running)
        finally:
            show.stop()


class ViewerWindow(unittest.TestCase):
    """Geometry, read back off the real canvas rather than eyeballed."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()
        try:
            import tkinter
        except ImportError as error:
            raise unittest.SkipTest(f"no tkinter: {error}")
        try:
            tkinter.Tk().destroy()
        except Exception as error:
            raise unittest.SkipTest(f"no display: {error}")

    def setUp(self):
        from eclipse_dmx.viewer import ViewerApp

        self.app = ViewerApp(RIG, pattern="obelisk_seasons")

    def tearDown(self):
        self.app._quit()

    def settle(self, seconds=0.8):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.app.root.update()
            time.sleep(0.02)

    def test_every_fixture_fits_at_any_window_size(self):
        from eclipse_dmx.viewer import GLOW_EXTENT

        for width, height in [(1000, 420), (640, 320), (1600, 900)]:
            with self.subTest(size=(width, height)):
                self.app.root.geometry(f"{width}x{height}")
                self.settle(0.6)

                canvas_w = self.app._panels[0].canvas.winfo_width()
                canvas_h = self.app._panels[0].canvas.winfo_height()
                self.assertEqual(len(self.app._panels[0]._items), 10)

                cores = [self.app._panels[0].canvas.coords(item["core"]) for item in self.app._panels[0]._items]
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

        self.settle(0.6)
        cores = [self.app._panels[0].canvas.coords(item["core"]) for item in self.app._panels[0]._items]
        centres = [((x0 + x1) / 2, (y0 + y1) / 2) for x0, y0, x1, y1 in cores]
        glow = (cores[0][2] - cores[0][0]) / 2 * GLOW_EXTENT

        gaps = [
            ((centres[i][0] - centres[i + 1][0]) ** 2 + (centres[i][1] - centres[i + 1][1]) ** 2) ** 0.5
            for i in range(len(centres) - 1)
        ]
        self.assertGreaterEqual(min(gaps), 2 * glow - 1.0)

    def test_colour_reaches_the_canvas(self):
        from eclipse_dmx.viewer import BACKGROUND

        self.settle(1.5)
        fills = [self.app._panels[0].canvas.itemcget(item["core"], "fill") for item in self.app._panels[0]._items]
        background = "#%02x%02x%02x" % BACKGROUND
        self.assertGreaterEqual(sum(1 for fill in fills if fill != background), 8)
        self.assertGreater(len(set(fills)), 2)

    def test_blackout_paints_the_rig_dark(self):
        self.settle(0.8)
        self.app._toggle_blackout()
        self.settle(0.8)
        fills = [self.app._panels[0].canvas.itemcget(item["core"], "fill") for item in self.app._panels[0]._items]
        self.assertTrue(all(fill == "#000000" for fill in fills))

    def test_pattern_cycling(self):
        self.settle(0.5)
        before = self.app.current_pattern
        self.app._step_pattern(1)
        self.settle(0.4)
        self.assertNotEqual(self.app.current_pattern, before)
        self.assertEqual(self.app._status, "")


class ViewerOnTheObelisk(unittest.TestCase):
    """344 pixels in a window that was built for ten pars."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()
        try:
            import tkinter
        except ImportError as error:
            raise unittest.SkipTest(f"no tkinter: {error}")
        try:
            tkinter.Tk().destroy()
        except Exception as error:
            raise unittest.SkipTest(f"no display: {error}")

    def setUp(self):
        from eclipse_dmx.viewer import ViewerApp

        self.app = ViewerApp(OBELISK, pattern="obelisk_seasons")

    def tearDown(self):
        self.app._quit()

    def settle(self, seconds=1.2):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.app.root.update()
            time.sleep(0.02)

    def test_every_pixel_is_drawn_once(self):
        self.settle(0.8)
        self.assertEqual(len(self.app._panels[0]._items), OBELISK_PIXELS)

    def test_dense_rigs_drop_the_glow(self):
        """3096 canvas items per repaint does not fit in a frame."""
        self.settle(0.8)
        self.assertTrue(all(not item["rings"] for item in self.app._panels[0]._items))

    def test_pixels_stay_inside_the_canvas(self):
        for width, height in [(1000, 600), (640, 320), (1600, 900)]:
            with self.subTest(size=(width, height)):
                self.app.root.geometry(f"{width}x{height}")
                self.settle(0.6)

                canvas_w = self.app._panels[0].canvas.winfo_width()
                canvas_h = self.app._panels[0].canvas.winfo_height()
                for item in self.app._panels[0]._items:
                    x0, y0, x1, y1 = self.app._panels[0].canvas.coords(item["core"])
                    self.assertGreaterEqual(x0, -0.5)
                    self.assertLessEqual(x1, canvas_w + 0.5)
                    self.assertGreaterEqual(y0, -0.5)
                    self.assertLessEqual(y1, canvas_h + 0.5)

    def test_the_four_sides_are_visibly_different(self):
        """The picture shows what the coordinates promised."""
        from eclipse_dmx.viewer import BACKGROUND

        self.settle(2.0)
        fills = [self.app._panels[0].canvas.itemcget(item["core"], "fill") for item in self.app._panels[0]._items]
        self.assertNotEqual(fills[0], "#%02x%02x%02x" % BACKGROUND)

        sides = [fills[side * 2 * OBELISK_SIDE_LENGTH + 20] for side in range(4)]
        self.assertEqual(len(set(sides)), 4, f"sides are not distinct: {sides}")

    def test_a_repeated_frame_is_not_repainted(self):
        """The pump runs at 60Hz over a 30fps stream; half of it is redundant."""
        self.settle(1.0)
        painted = self.app._painted
        self.assertIsNotNone(painted)

        self.app._paint(painted)
        self.assertIs(self.app._painted, painted)

    def test_the_link_row_is_only_there_for_a_relic(self):
        """A dead row of buttons on a DMX rig is furniture that does nothing."""
        self.settle(0.8)
        self.assertFalse(self.app.link_row.winfo_ismapped())

    def test_runs_are_labelled_rather_than_pixels(self):
        """Eight legends, not 344."""
        self.settle(0.8)
        texts = [
            self.app._panels[0].canvas.itemcget(item, "text")
            for item in self.app._panels[0].canvas.find_all()
            if self.app._panels[0].canvas.type(item) == "text"
        ]
        self.assertEqual(
            sorted(texts),
            ["a_down", "a_up", "b_down", "b_up", "c_down", "c_up", "d_down", "d_up"],
        )


class ViewerOnARelic(unittest.TestCase):
    """The relic row, on a config that names one.

    Dry-run, so no cable is opened and the executable is on `console` - the row
    is drawn from the *config*, which is what a desk needs to see before it
    plugs anything in.
    """

    @classmethod
    def setUpClass(cls):
        executable_or_skip()
        try:
            import tkinter
        except ImportError as error:
            raise unittest.SkipTest(f"no tkinter: {error}")
        try:
            tkinter.Tk().destroy()
        except Exception as error:
            raise unittest.SkipTest(f"no display: {error}")

    def setUp(self):
        from eclipse_dmx.viewer import ViewerApp

        self.app = ViewerApp(OBELISK_USB, pattern="obelisk_seasons")

    def tearDown(self):
        self.app._quit()

    def settle(self, seconds=1.0):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.app.root.update()
            time.sleep(0.02)

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


class ViewerOnTheShow(unittest.TestCase):
    """The tempo controls, driven the way a click drives them."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()
        try:
            import tkinter
        except ImportError as error:
            raise unittest.SkipTest(f"no tkinter: {error}")
        try:
            tkinter.Tk().destroy()
        except Exception as error:
            raise unittest.SkipTest(f"no display: {error}")

    def setUp(self):
        from eclipse_dmx.viewer import ViewerApp

        self.app = ViewerApp(SHOW, midi="", bpm=120.0)

    def tearDown(self):
        self.app._quit()

    def settle(self, seconds=0.8):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.app.root.update()
            time.sleep(0.02)

    def test_the_show_states_have_buttons(self):
        self.settle(0.8)
        for state in MYTHOS26_STATES:
            self.assertIn(state, self.app._state_buttons)

    def test_the_tempo_reaches_the_header(self):
        self.settle(1.5)
        self.assertIn("120.0 bpm", self.app.header.cget("text"))

    def test_the_tempo_buttons_take(self):
        self.settle(0.8)
        self.app._run_button(("bpm", "100"))
        self.settle(1.2)
        self.assertEqual(self.app._status, "")
        self.assertIn("100.0 bpm", self.app.header.cget("text"))

    def test_tapping_does_not_error(self):
        self.settle(0.5)
        self.app._run_button(("beat", ""))
        self.assertEqual(self.app._status, "")

        # The beat line re-announces on its own cadence, so "manual" lands in
        # the header a beat or two after the tap - poll rather than guess the
        # one sleep that always wins the race.
        for _ in range(12):
            self.settle(0.5)
            if "manual" in self.app.header.cget("text"):
                break
        self.assertIn("manual", self.app.header.cget("text"))

    def test_the_master_slider_reaches_the_rig(self):
        self.settle(0.8)
        self.app._on_master_slider("40")
        self.settle(0.5)
        self.assertEqual(self.app._status, "")
        self.assertAlmostEqual(self.app._master, 0.4, places=2)
        self.assertIn("master=0.4", self.app.show.status())

    def test_the_arrow_keys_and_the_slider_agree(self):
        """Two controls on one value; they must not drift apart."""
        self.settle(0.8)
        self.app._on_master_slider("50")
        self.settle(0.3)
        self.app._nudge_master(0.1)
        self.settle(0.3)
        self.assertAlmostEqual(self.app._master, 0.6, places=2)
        self.assertAlmostEqual(self.app._master_var.get(), 60.0, delta=1.0)

    def test_dragging_the_slider_does_not_feed_itself(self):
        """_set_master writes the variable back, which re-enters the handler."""
        self.settle(0.5)
        self.app._on_master_slider("70")
        self.settle(0.3)
        self.assertAlmostEqual(self.app._master, 0.7, places=2)
        self.assertEqual(self.app._status, "")

    def test_the_looks_knobs_get_controls(self):
        self.settle(1.0)
        self.assertIn("attack", self.app._param_widgets)
        self.assertIn("hold", self.app._param_widgets)

        # a float gets a slider and a box; a bool gets neither box nor range
        self.assertIsNotNone(self.app._param_widgets["attack"][1])
        self.assertIsNone(self.app._param_widgets["hold"][1])

    def test_a_cue_change_rebuilds_the_panel(self):
        self.settle(1.0)
        self.assertNotIn("base_gain", self.app._param_widgets)

        self.app._run_button(("state", "vu_pulse"))
        self.settle(1.0)

        self.assertIn("base_gain", self.app._param_widgets)
        self.assertEqual(self.app._status, "")

    def test_a_colour_knob_gets_a_swatch_of_its_own_colour(self):
        self.settle(1.0)
        _, swatch = self.app._param_widgets["color"]
        self.assertIsNotNone(swatch)
        self.assertEqual(str(swatch.cget("bg")), "#ffffff")

    def test_a_picked_colour_reaches_the_look_and_the_swatch(self):
        """The picker itself is the system's; everything either side is ours."""
        from eclipse_dmx import viewer

        self.settle(1.0)
        picked = []

        def fake_picker(*args, **kwargs):
            picked.append(kwargs.get("color"))
            return ((0, 64, 255), "#0040ff")

        original = viewer.colorchooser.askcolor
        viewer.colorchooser.askcolor = fake_picker
        try:
            self.app._pick_color("color")
            self.settle(0.5)
        finally:
            viewer.colorchooser.askcolor = original

        # Opened on the colour the look is actually showing, not on a default.
        self.assertEqual(picked, ["#ffffff"])
        self.assertEqual(self.app.show.get_param("color").value, "#0040ff")
        self.assertEqual(str(self.app._param_widgets["color"][1].cget("bg")), "#0040ff")
        self.assertEqual(self.app._status, "")

    def test_a_cancelled_pick_changes_nothing(self):
        from eclipse_dmx import viewer

        self.settle(1.0)
        before = self.app.show.get_param("color").value

        original = viewer.colorchooser.askcolor
        viewer.colorchooser.askcolor = lambda *args, **kwargs: (None, None)
        try:
            self.app._pick_color("color")
            self.settle(0.4)
        finally:
            viewer.colorchooser.askcolor = original

        self.assertEqual(self.app.show.get_param("color").value, before)

    def test_a_slider_reaches_the_look(self):
        self.settle(1.0)
        self.app._on_param_slider("floor", "0.75")
        self.settle(0.5)
        self.assertEqual(self.app._status, "")
        self.assertAlmostEqual(self.app.show.get_param("floor").value, 0.75, places=2)

    def test_the_box_takes_a_value_the_slider_cannot_land_on(self):
        """The whole reason there is a box beside the slider."""
        self.settle(1.0)
        _, entry = self.app._param_widgets["decay"]
        entry.delete(0, "end")
        entry.insert(0, "0.137")
        self.app._on_param_entry("decay")
        self.settle(0.5)
        self.assertAlmostEqual(self.app.show.get_param("decay").value, 0.137, places=3)

    def test_nonsense_in_the_box_puts_the_real_value_back(self):
        self.settle(1.0)
        _, entry = self.app._param_widgets["decay"]
        live = self.app.show.get_param("decay").value

        entry.delete(0, "end")
        entry.insert(0, "banana")
        self.app._on_param_entry("decay")
        self.settle(0.4)

        self.assertAlmostEqual(float(entry.get()), live, places=3)
        self.assertAlmostEqual(self.app.show.get_param("decay").value, live, places=3)


class LookParams(unittest.TestCase):
    """The per-look tuning surface, over the protocol."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def _show(self, **kwargs):
        return ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None,
                              emit_rate=20.0, **kwargs)

    def test_the_running_look_announces_its_knobs(self):
        show = self._show()
        try:
            time.sleep(0.6)
            names = [param.name for param in show.params]
            self.assertEqual(names, ["attack", "decay", "intensity", "floor",
                                     "hold", "rate", "color"])

            attack = show.get_param("attack")
            self.assertEqual(attack.kind, "f")
            self.assertFalse(attack.is_bool)
            self.assertTrue(show.get_param("hold").is_bool)
            self.assertTrue(show.get_param("color").is_color)
        finally:
            show.stop()

    def test_a_cue_change_replaces_the_set(self):
        """The knobs belong to the look, not to the pattern."""
        show = self._show()
        try:
            time.sleep(0.6)
            first = show.params_revision

            show.set_state("vu_pulse")
            time.sleep(0.5)
            self.assertGreater(show.params_revision, first)
            self.assertIn("base_gain", [param.name for param in show.params])

            show.set_state("tv_static")
            time.sleep(0.5)
            self.assertEqual([param.name for param in show.params], ["monochrome", "floor"])
        finally:
            show.stop()

    def test_a_value_out_of_range_is_clamped_not_refused(self):
        show = self._show()
        try:
            time.sleep(0.6)
            show.set_param("attack", 99.0)
            self.assertAlmostEqual(show.get_param("attack").value, 1.0, places=3)
            show.set_param("attack", -5.0)
            self.assertAlmostEqual(show.get_param("attack").value, 0.0, places=3)
        finally:
            show.stop()

    def test_the_echo_lands_before_the_reply(self):
        """A client that waits on OK and then reads must see the new value.

        Emitted the other way round, this passes about half the time, which is
        the worst kind of protocol bug to own.
        """
        show = self._show()
        try:
            time.sleep(0.6)
            for target in (0.11, 0.22, 0.33, 0.44):
                show.set_param("decay", target)
                self.assertAlmostEqual(show.get_param("decay").value, target, places=3)
        finally:
            show.stop()

    def test_setting_a_knob_does_not_duplicate_the_set(self):
        """The echo updates in place; it does not append to the list."""
        show = self._show()
        try:
            time.sleep(0.8)          # frame lines between the set and the echo
            before = len(show.params)
            show.set_param("floor", 0.5)
            show.set_param("floor", 0.6)
            time.sleep(0.3)
            self.assertEqual(len(show.params), before)
        finally:
            show.stop()

    def test_a_knob_reaches_the_render(self):
        """The point of the whole thing: the value is the look's own field."""
        frames = []
        show = ShowController(SHOW, dry_run=True, midi="", bpm=128.0,
                              on_frame=frames.append, emit_rate=40.0)
        try:
            time.sleep(1.0)
            frames.clear()
            time.sleep(0.8)
            dark = min(max(frame[0]) for frame in frames)

            show.set_param("floor", 0.8)
            time.sleep(0.3)
            frames.clear()
            time.sleep(0.8)
            lifted = min(max(frame[0]) for frame in frames)

            self.assertLess(dark, 40)
            self.assertGreater(lifted, 120)
        finally:
            show.stop()

    def test_a_bool_takes_on_off_and_reaches_the_render(self):
        frames = []
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=frames.append,
                              emit_rate=40.0)
        try:
            show.set_state("tv_static_mono")
            time.sleep(0.8)
            self.assertTrue(show.get_param("monochrome").value)

            show.set_param("monochrome", False)
            time.sleep(0.3)
            frames.clear()
            time.sleep(0.5)

            self.assertFalse(show.get_param("monochrome").value)
            self.assertTrue(any(len(set(colour)) > 1 for frame in frames for colour in frame),
                            "still grey after monochrome was turned off")
        finally:
            show.stop()

    def test_a_name_that_is_not_a_knob_is_rejected_without_dying(self):
        show = self._show()
        try:
            time.sleep(0.6)
            with self.assertRaises(ShowError):
                show.set_param("not_a_knob", 1.0)
            self.assertTrue(show.is_running)
        finally:
            show.stop()

    def test_dump_is_the_running_look_as_json(self):
        show = self._show()
        try:
            time.sleep(0.6)
            show.set_param("attack", 0.25)
            dump = json.loads(show.dump_params())
            self.assertAlmostEqual(dump["attack"], 0.25, places=3)
            self.assertIsInstance(dump["hold"], bool)
        finally:
            show.stop()

    def test_a_colour_knob_round_trips_as_hex(self):
        show = self._show()
        try:
            time.sleep(0.6)
            self.assertEqual(show.get_param("color").value, "#ffffff")

            show.set_param("color", "#0040ff")
            self.assertEqual(show.get_param("color").value, "#0040ff")
        finally:
            show.stop()

    def _peak_colour(self, state, knobs):
        """The lit frame's colour, on the rig, with those knobs set."""
        frames = []
        show = ShowController(SHOW, dry_run=True, midi="", bpm=128.0,
                              on_frame=frames.append, emit_rate=40.0)
        try:
            show.set_state(state)
            for name, value in knobs.items():
                show.set_param(name, value)
            time.sleep(0.8)         # past the cross-fade
            frames.clear()
            time.sleep(1.0)
        finally:
            show.stop()
        self.assertTrue(frames, "no frames arrived")
        return max(frames, key=lambda frame: max(frame[0]))[0]

    def test_a_colour_knob_reaches_the_render(self):
        red, green, blue = self._peak_colour("beat_pulse", {"color": "#00ff00"})
        self.assertGreater(green, 200)
        self.assertLess(red, 40)
        self.assertLess(blue, 40)

    def test_a_flash_colour_survives_the_vu_composite(self):
        """The layer above the wash is the flash's colour, not white.

        The composite desaturates rather than blends, which arrives at exactly
        white for the white it opens on. A flash with a hue of its own has to
        arrive at *that*, or the picker is a lie on this look.
        """
        red, green, blue = self._peak_colour("vu_pulse", {"color": "#00ff00"})
        self.assertGreater(green, 200)
        self.assertLess(red, 40)
        self.assertLess(blue, 40)

    def test_flash_intensity_takes_the_hit_down_not_the_rig(self):
        """It scales the hit, and the floor holding the rig up is left alone."""
        def peak(knobs):
            frames = []
            show = ShowController(SHOW, dry_run=True, midi="", bpm=128.0,
                                  on_frame=frames.append, emit_rate=40.0)
            try:
                show.set_state("vu_pulse")
                for name, value in knobs.items():
                    show.set_param(name, value)
                time.sleep(0.8)
                frames.clear()
                time.sleep(1.0)
            finally:
                show.stop()
            self.assertTrue(frames, "no frames arrived")
            return (max(max(frame[0]) for frame in frames),
                    min(max(frame[0]) for frame in frames))

        full, _ = peak({})
        half, _ = peak({"intensity": 0.5})
        none, floor_lit = peak({"intensity": 0.0, "floor": 0.6})

        self.assertGreater(full, 200)

        # Half the hit is dimmer, but not by half on the wire: the show's
        # master gamma of 2.2 sits between the two, so 0.5 arrives at about
        # 0.5^2.2. The bounds are loose because what is being tested is that
        # the knob reaches the render, not what gamma does.
        self.assertLess(half, full * 0.75)
        self.assertGreater(half, full * 0.10)

        # No flash left, but the floor is still lighting the rig - and flat,
        # because there is no hit on top of it to move it.
        self.assertGreater(floor_lit, 60)
        self.assertLess(none - floor_lit, 20)

    def test_the_composite_never_invents_a_third_colour(self):
        """Blue over red went through *green* on the way. Nobody put green here.

        The regression that took the composite from lerping hues to blending
        chroma vectors: red to blue around the rim of the wheel passes through
        green, and through the middle it passes through pale magenta - which is
        what a flash washing a colour out looks like, and is also exactly the
        desaturation this look does with the white it opens on.
        """
        frames = []
        show = ShowController(SHOW, dry_run=True, midi="", bpm=128.0,
                              on_frame=frames.append, emit_rate=40.0)
        try:
            show.set_state("vu_pulse")
            show.set_param("color", "#0000ff")
            time.sleep(1.0)     # past the cue's cross-fade, which blends its own way
            frames.clear()
            time.sleep(1.5)
        finally:
            show.stop()

        self.assertTrue(frames, "no frames arrived")

        # Green-dominant, and bright enough that it is a colour rather than
        # rounding on a nearly dark fixture.
        greenish = [colour for colour in (frame[0] for frame in frames)
                    if colour[1] > colour[0] and colour[1] > colour[2] and colour[1] > 24]
        self.assertFalse(greenish,
                         f"green between a red wash and a blue flash: {greenish[:6]}")

    def test_a_colour_that_is_not_one_is_rejected_without_dying(self):
        show = self._show()
        try:
            time.sleep(0.6)
            with self.assertRaises(ShowError):
                show.set_param("color", "banana")
            self.assertTrue(show.is_running)

            # And the other way round: a number is not a colour either.
            with self.assertRaises(ShowError):
                show.set_param("color", 0.5)
            self.assertTrue(show.is_running)
        finally:
            show.stop()

    def test_a_hex_string_is_not_a_number_knob(self):
        """`param attack #ff0000` is a mistake, not a conversion."""
        show = self._show()
        try:
            time.sleep(0.6)
            with self.assertRaises(ShowError):
                show.set_param("attack", "#ff0000")
            self.assertTrue(show.is_running)
        finally:
            show.stop()

    def test_dump_keeps_a_colour_as_a_string(self):
        """The point of a dump is that it can be pasted back into a config."""
        show = self._show()
        try:
            time.sleep(0.6)
            show.set_param("color", "#123456")
            dump = json.loads(show.dump_params())
            self.assertEqual(dump["color"], "#123456")
        finally:
            show.stop()

    def test_a_plain_pattern_offers_the_three_it_has(self):
        show = self._show()
        try:
            time.sleep(0.6)
            show.set_pattern("rainbow")
            time.sleep(0.5)
            self.assertEqual([param.name for param in show.params],
                             ["speed", "width", "brightness"])
        finally:
            show.stop()


class ScannerKnobs(unittest.TestCase):
    """The scanner looks' tuning surface - the knobs the curve editor drives."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def _show(self, **kwargs):
        return ShowController(SCANNER, dry_run=True, midi="", on_frame=lambda f: None,
                              emit_rate=20.0, **kwargs)

    def test_the_boot_swell_has_two_independent_knobs(self):
        show = self._show()
        try:
            show.set_state("boot")
            time.sleep(0.8)
            self.assertEqual([param.name for param in show.params],
                             ["boot_time", "noise"])
        finally:
            show.stop()

    def test_the_recording_states_share_one_wave_and_its_knobs(self):
        """Unified look, unified surface: both states offer the same wave."""
        show = self._show()
        try:
            show.set_state("scan_item_detected_filter")
            time.sleep(0.8)
            wave = [param.name for param in show.params]
            self.assertEqual(wave, ["wave_scale", "rise_speed"])

            show.set_state("audio_playback_recording")
            time.sleep(0.8)
            self.assertEqual([param.name for param in show.params], wave)
        finally:
            show.stop()

    def test_the_recording_flow_offers_its_shapes(self):
        show = self._show()
        try:
            show.set_state("record_arm")
            time.sleep(0.8)
            self.assertEqual([param.name for param in show.params],
                             ["rate", "floor", "gain"])

            show.set_state("record_active")
            time.sleep(0.8)
            self.assertEqual([param.name for param in show.params],
                             ["orbit_rate", "tail"])

            show.set_state("record_countdown")
            time.sleep(0.8)
            self.assertEqual([param.name for param in show.params], ["sweep"])
        finally:
            show.stop()

    def test_a_scanner_knob_reaches_the_render(self):
        """floor 1, gain 0 flattens record_arm's breath to a steady amber."""
        frames = []
        show = ShowController(SCANNER, dry_run=True, midi="", on_frame=frames.append,
                              emit_rate=20.0)
        try:
            show.set_state("record_arm")
            time.sleep(1.2)          # past the cue blend
            show.set_param("floor", 1.0)
            show.set_param("gain", 0.0)
            time.sleep(0.3)
            frames.clear()
            time.sleep(0.8)

            reds = [frame[0][0] for frame in frames]
            self.assertGreater(min(reds), 200, "not at full amber")
            self.assertLessEqual(max(reds) - min(reds), 2, "still breathing")
        finally:
            show.stop()


class TheCurveProtocol(unittest.TestCase):
    """Live shapes over the wire: CURVE announcements and the `curve` command.

    The point of the whole channel: a look's envelope is an AutomationCurve,
    and the desk's curve editor loads the live shape and writes an edited one
    back - so the protocol must announce shapes with the knobs and accept a
    whole shape at once.
    """

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def test_the_pulse_announces_its_envelope(self):
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None,
                              emit_rate=20.0)
        try:
            time.sleep(0.6)
            self.assertIn("envelope", show.curves)
            self.assertGreaterEqual(len(show.curves["envelope"]), 3)

            # vu_pulse forwards the same flash, so the same shape shows there
            show.set_state("vu_pulse")
            time.sleep(0.5)
            self.assertIn("envelope", show.curves)
        finally:
            show.stop()

    def test_a_drawn_shape_lands_and_echoes(self):
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None,
                              emit_rate=20.0)
        try:
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
        finally:
            show.stop()

    def test_a_shape_reaches_the_render(self):
        """Flatten the boot swell's brightness curve to zero: the rig darkens."""
        frames = []
        show = ShowController(SCANNER, dry_run=True, midi="", on_frame=frames.append,
                              emit_rate=20.0)
        try:
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
        finally:
            show.stop()

    def test_an_unknown_curve_is_refused_without_dying(self):
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None,
                              emit_rate=20.0)
        try:
            time.sleep(0.6)
            with self.assertRaises(ShowError):
                show.set_curve("not_a_curve", [(0.0, 0.0, None), (1.0, 1.0, None)])
            self.assertTrue(show.is_running)
        finally:
            show.stop()

    def test_a_ninth_key_is_refused_whole(self):
        """Too many keys rejects the message; the look keeps its old shape."""
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None,
                              emit_rate=20.0)
        try:
            time.sleep(0.6)
            before = show.curves["envelope"]
            with self.assertRaises(ShowError):
                show.set_curve("envelope",
                               [(i * 0.1, 0.5, None) for i in range(9)])
            self.assertEqual(show.curves["envelope"], before)
        finally:
            show.stop()


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


if __name__ == "__main__":
    unittest.main()
