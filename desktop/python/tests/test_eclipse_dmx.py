"""Tests for the python wrapper and the viewer.

Plain unittest, so this needs nothing installed:

    cd desktop
    python -m unittest discover -s python/tests -v

Anything that needs the executable or a display skips itself when there is not
one, so this stays runnable on a build machine and on a show laptop.
"""

from __future__ import annotations

import sys
import time
import unittest
from pathlib import Path

DESKTOP = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DESKTOP / "python"))

from eclipse_dmx.binary import BinaryNotFoundError, find_executable  # noqa: E402
from eclipse_dmx.config import Config, ConfigError, Fixture  # noqa: E402
from eclipse_dmx.controller import ShowController, ShowError, _parse_frame  # noqa: E402

RIG = DESKTOP / "config" / "uking_par36_x10.json"


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

                canvas_w = self.app.canvas.winfo_width()
                canvas_h = self.app.canvas.winfo_height()
                self.assertEqual(len(self.app._items), 10)

                cores = [self.app.canvas.coords(item["core"]) for item in self.app._items]
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
        cores = [self.app.canvas.coords(item["core"]) for item in self.app._items]
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
        fills = [self.app.canvas.itemcget(item["core"], "fill") for item in self.app._items]
        background = "#%02x%02x%02x" % BACKGROUND
        self.assertGreaterEqual(sum(1 for fill in fills if fill != background), 8)
        self.assertGreater(len(set(fills)), 2)

    def test_blackout_paints_the_rig_dark(self):
        self.settle(0.8)
        self.app._toggle_blackout()
        self.settle(0.8)
        fills = [self.app.canvas.itemcget(item["core"], "fill") for item in self.app._items]
        self.assertTrue(all(fill == "#000000" for fill in fills))

    def test_pattern_cycling(self):
        self.settle(0.5)
        before = self.app.current_pattern
        self.app._step_pattern(1)
        self.settle(0.4)
        self.assertNotEqual(self.app.current_pattern, before)
        self.assertEqual(self.app._status, "")


if __name__ == "__main__":
    unittest.main()
