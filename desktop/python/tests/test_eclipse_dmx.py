"""Tests for the python wrapper and the viewer.

Plain unittest, so this needs nothing installed:

    cd desktop
    python -m unittest discover -s python/tests -v

Anything that needs the executable or a display skips itself when there is not
one, so this stays runnable on a build machine and on a show laptop.
"""

from __future__ import annotations

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

RIG = DESKTOP / "config" / "uking_par36_x10.json"
SHOW = DESKTOP / "config" / "mythos26.json"


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


class BeatDivision(unittest.TestCase):
    """on 1 / on 2 / on 4, and each look's own default."""

    @classmethod
    def setUpClass(cls):
        executable_or_skip()

    def _count(self, state, seconds=4.0, division=None, bpm=120.0):
        frames = []
        show = ShowController(
            SHOW, dry_run=True, midi="", bpm=bpm, on_frame=frames.append, emit_rate=40.0
        )
        try:
            show.set_state(state)
            if division is not None:
                show.set_beat_division(division)
            frames.clear()          # drop the cross-fade
            time.sleep(seconds)
        finally:
            show.stop()
        return _rising_edges(frames)

    def test_vu_pulse_opens_on_twos(self):
        """Its own default, without anyone selecting a division."""
        beats = 120.0 / 60.0 * 4.0          # 8 beats in the window
        self.assertAlmostEqual(self._count("vu_pulse"), beats / 2, delta=1.5)

    def test_beat_pulse_opens_on_every_beat(self):
        beats = 120.0 / 60.0 * 4.0
        self.assertAlmostEqual(self._count("beat_pulse"), beats, delta=2.0)

    def test_the_selection_overrides_the_default(self):
        """`on 1` should make vu_pulse fire every beat despite opening on twos."""
        beats = 120.0 / 60.0 * 4.0
        self.assertAlmostEqual(self._count("vu_pulse", division=1), beats, delta=2.0)

    def test_on_four_is_once_a_bar(self):
        beats = 120.0 / 60.0 * 8.0
        self.assertAlmostEqual(
            self._count("beat_pulse", seconds=8.0, division=4), beats / 4, delta=1.5
        )

    def test_zero_hands_back_each_looks_default(self):
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None)
        try:
            show.set_state("vu_pulse")
            show.set_beat_division(1)
            show.set_beat_division(0)
            time.sleep(0.3)
            self.assertIn("div=2", show.status(), "vu_pulse should be back on twos")
        finally:
            show.stop()

    def test_a_nonsense_division_is_rejected_without_dying(self):
        show = ShowController(SHOW, dry_run=True, midi="", on_frame=lambda f: None)
        try:
            with self.assertRaises(ShowError):
                show.command("beat div nonsense")
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
        """Otherwise it is a flashing rig, not static."""
        frames = self._frames("tv_static_mono")
        varied = sum(1 for frame in frames if len(set(frame)) > len(frame) // 2)
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
        for path in (SHOW, DESKTOP / "config" / "mythos26_mixxx.json"):
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
        self.settle(0.5)
        self.assertEqual(self.app._status, "")
        self.assertIn("manual", self.app.header.cget("text"))

    def test_the_division_buttons_take(self):
        self.settle(0.8)
        self.app._run_button(("div", "4"))
        self.settle(0.5)
        self.assertEqual(self.app._status, "")
        self.assertIn("on 4", self.app.header.cget("text"))
        self.assertIn("div=4", self.app.show.status())

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
            self.assertEqual(names, ["attack", "decay", "floor", "hold"])

            attack = show.get_param("attack")
            self.assertEqual(attack.kind, "f")
            self.assertFalse(attack.is_bool)
            self.assertTrue(show.get_param("hold").is_bool)
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


if __name__ == "__main__":
    unittest.main()
