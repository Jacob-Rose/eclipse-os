"""Command line front end: ``python -m eclipse_dmx``.

Thin on purpose. This is the "does my rig work" tool, not a console.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List, Optional

from .binary import BinaryNotFoundError, find_executable
from .config import BUILTIN_PALETTES, BUILTIN_PROFILES, PATTERN_NAMES, Config, ConfigError
from .controller import ShowController, ShowError
from .ports import list_midi_ports, list_ports


def _cmd_ports(args: argparse.Namespace) -> int:
    ports = list_ports(args.executable)
    if not ports:
        print("no serial ports found", file=sys.stderr)
        return 1
    for port in ports:
        print(port)
    return 0


def _cmd_midi(args: argparse.Namespace) -> int:
    ports = list_midi_ports(args.executable)
    if not ports:
        print("no MIDI inputs found", file=sys.stderr)
        return 1
    for port in ports:
        print(port)
    return 0


def _cmd_midi_watch(args: argparse.Namespace) -> int:
    """Prints what a MIDI input is actually sending.

    The setup tool. Every step of wiring Mixxx up is verifiable except the last
    one — whether the notes are the notes we think they are — and this is that
    step. Play a track and read what comes out.
    """
    config = Config.load(args.config)

    seen: dict = {}
    beats = [0]

    def note(payload: str) -> None:
        parts = payload.split()
        if len(parts) < 4 or parts[1] != "note_on":
            return
        key = (parts[0], parts[2])          # channel, note number
        seen[key] = seen.get(key, 0) + 1
        if not args.quiet:
            print(payload)

    def beat(index: int, bpm: float) -> None:
        beats[0] = index

    show = ShowController(
        args.config,
        executable=args.executable,
        dry_run=True,                       # a diagnostic must not light a rig
        midi=args.midi,
        on_midi=note,
        on_beat=beat,
    )

    with show:
        show.midi_monitor(True)
        print(f"listening on {show.midi_status()}", file=sys.stderr)
        print(f"play a track for {args.seconds:.0f}s...", file=sys.stderr)
        show.wait(args.seconds)
        counts = dict(seen)
        status = show.midi_status()

    print()
    if not counts:
        print("nothing arrived.", file=sys.stderr)
        print(
            "  - is loopMIDI running, and was it running before Mixxx started?\n"
            "  - Mixxx: Preferences > Controllers, is the port enabled and the\n"
            "    'MIDI for light' mapping loaded and applied?\n"
            "  - is a track actually playing?",
            file=sys.stderr,
        )
        return 1

    print("notes seen (channel, note, count):")
    for (channel, number), count in sorted(counts.items(), key=lambda kv: -kv[1]):
        label = ""
        if int(number) == config.midi.beat_note:
            label = "  <- taken as the beat"
        elif int(number) == config.midi.bpm_note:
            label = "  <- taken as the tempo"
        print(f"  ch{channel:<4} note {number:<5} x{count}{label}")

    print()
    print(status)

    if beats[0] == 0:
        print(
            "\nno beats registered. Set midi.beat_note to whichever note above "
            "arrives once per beat,\nand midi.beat_channel to its channel.",
            file=sys.stderr,
        )
        return 1

    return 0


def _cmd_validate(args: argparse.Namespace) -> int:
    config = Config.load(args.config)
    warnings = config.validate(strict_overlap=not args.allow_overlap)

    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)

    print(f"ok: {len(config.fixtures)} fixtures, pattern '{config.pattern.name}', "
          f"device '{config.device.type}' on '{config.device.port}'")
    return 0


def _cmd_generate(args: argparse.Namespace) -> int:
    """Writes a config for a bank of identical fixtures."""
    config = Config()
    config.device.type = args.device
    config.device.port = args.port
    config.pattern.name = args.pattern

    if args.profile:
        config.add_bank(args.profile, count=args.count, address=args.first_channel)
    else:
        config.add_rgb_bank(
            count=args.count,
            first_channel=args.first_channel,
            channels_per_fixture=args.channels_per_fixture,
            order=args.order,
        )

    if args.output == "-":
        print(config.to_json())
    else:
        path = config.write(args.output)
        print(f"wrote {path} ({len(config.fixtures)} fixtures)")
    return 0


def _cmd_patch(args: argparse.Namespace) -> int:
    """Prints the resolved channel map, so a patch can be checked on paper."""
    config = Config.load(args.config)
    config.validate(strict_overlap=not args.allow_overlap)

    # Reported in the config's own numbering, so these line up with what is
    # dialled on the fixtures. Matches the executable's --show-patch.
    show = config.display_channel
    print(f"addressing: {config.addressing}-based")
    print()

    highest = 0
    for fixture in config.fixtures:
        r, g, b = (show(fixture.start_channel + offset) for offset in fixture.offsets())
        line = f"{fixture.name:<16} r={r:<4} g={g:<4} b={b:<4}"
        if fixture.dimmer_channel:
            line += f" dimmer={show(fixture.dimmer_channel)}@{fixture.dimmer_value}"
        if fixture.static_channels:
            parked = " ".join(
                f"park[{show(c)}]={v}" for c, v in sorted(fixture.static_channels.items())
            )
            line += f" {parked}"
        print(line)
        highest = max(highest, max(fixture.used_channels()))

    print()
    print(f"{len(config.fixtures)} fixtures, highest channel {show(highest)}")
    return 0


def _cmd_run(args: argparse.Namespace) -> int:
    config_path = Path(args.config)

    def log(line: str) -> None:
        print(line, file=sys.stderr)

    show = ShowController(
        config_path,
        executable=args.executable,
        dry_run=args.dry_run,
        frames=args.frames,
        midi=args.midi,
        bpm=args.bpm,
        on_log=log if args.verbose else None,
    )

    with show:
        for warning in show.warnings:
            print(f"warning: {warning}", file=sys.stderr)

        if args.pattern:
            show.set_pattern(args.pattern)
        if args.state:
            show.set_state(args.state)
        if args.brightness is not None:
            show.set_master(args.brightness)

        print(f"running: {show.status()}", file=sys.stderr)

        code = show.wait(args.seconds)
        return 0 if code is None else code


def _cmd_view(args: argparse.Namespace) -> int:
    """Opens the viewer. Imported here so the rest of the CLI works headless."""
    try:
        from .viewer import view
    except ImportError as error:  # tkinter missing (some slim linux pythons)
        print(
            f"error: the viewer needs tkinter, which this python does not have ({error}).\n"
            f"       on debian/ubuntu: sudo apt install python3-tk",
            file=sys.stderr,
        )
        return 1

    return view(
        args.config,
        executable=args.executable,
        pattern=args.pattern,
        state=args.state,
        live=args.live,
        emit_rate=args.emit_rate,
        midi=args.midi,
        bpm=args.bpm,
    )


def _cmd_list(args: argparse.Namespace) -> int:
    print("patterns:")
    for name in PATTERN_NAMES:
        print(f"  {name}")
    print()
    print("palettes:")
    for name in BUILTIN_PALETTES:
        print(f"  {name}")
    print()
    print("fixture profiles:")
    for name, profile in BUILTIN_PROFILES.items():
        slots = ["-"] * profile.footprint
        slots[profile.red - 1] = "red"
        slots[profile.green - 1] = "green"
        slots[profile.blue - 1] = "blue"
        if profile.dimmer:
            slots[profile.dimmer - 1] = f"dimmer={profile.dimmer_value}"
        for offset, value in profile.park.items():
            slots[int(offset) - 1] = f"park={value}"
        chart = "  ".join(f"{i + 1}:{slot}" for i, slot in enumerate(slots))
        print(f"  {name} ({profile.footprint}ch)  {chart}")
    return 0


def _add_tempo_args(parser: argparse.ArgumentParser) -> None:
    """Tempo overrides, shared by the subcommands that start a show."""
    parser.add_argument("--midi", metavar="SPEC",
                        help="MIDI input to take the beat from: an index, a name, part of one, "
                             "or 'auto'. Overrides the config's midi block; "
                             "pass an empty string to open nothing.")
    parser.add_argument("--bpm", type=float,
                        help="starting tempo, and the one the rig falls back to if the "
                             "MIDI link goes quiet")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="eclipse_dmx",
        description="configure and drive eclipse-dmx, the eclipse-os DMX renderer",
    )
    parser.add_argument("--executable", help="path to eclipse-dmx (default: the build output, then PATH)")

    subparsers = parser.add_subparsers(dest="command", required=True)

    ports = subparsers.add_parser("ports", help="list serial ports the executable can see")
    ports.set_defaults(func=_cmd_ports)

    midi = subparsers.add_parser("midi", help="list MIDI inputs the executable can see")
    midi.set_defaults(func=_cmd_midi)

    watch = subparsers.add_parser(
        "midi-watch",
        help="print what a MIDI input is actually sending, and whether we read it as a beat",
    )
    watch.add_argument("config")
    watch.add_argument("--midi", metavar="SPEC",
                       help="override the config's midi.port")
    watch.add_argument("--seconds", type=float, default=10.0,
                       help="how long to listen (default 10)")
    watch.add_argument("--quiet", "-q", action="store_true",
                       help="only the summary, not every message")
    watch.set_defaults(func=_cmd_midi_watch)

    listing = subparsers.add_parser("list", help="list available patterns and palettes")
    listing.set_defaults(func=_cmd_list)

    validate = subparsers.add_parser("validate", help="check a config without touching hardware")
    validate.add_argument("config")
    validate.add_argument("--allow-overlap", action="store_true",
                          help="treat channel collisions as warnings instead of errors")
    validate.set_defaults(func=_cmd_validate)

    patch = subparsers.add_parser("patch", help="print the resolved channel map for a config")
    patch.add_argument("config")
    patch.add_argument("--allow-overlap", action="store_true")
    patch.set_defaults(func=_cmd_patch)

    generate = subparsers.add_parser("generate", help="write a config for a bank of fixtures")
    generate.add_argument("--count", type=int, required=True, help="how many fixtures")
    generate.add_argument("--profile", choices=sorted(BUILTIN_PROFILES),
                          help="fixture model to patch (e.g. uking_par36); "
                               "otherwise a plain RGB fixture is assumed")
    generate.add_argument("--first-channel", type=int, default=1,
                          help="DMX address of the first fixture")
    generate.add_argument("--channels-per-fixture", type=int, default=3,
                          help="address stride, when not using --profile")
    generate.add_argument("--order", default="rgb",
                          help="channel order, when not using --profile")
    generate.add_argument("--device", default="enttec_pro",
                          help="enttec_pro, enttec_open, console or preview")
    generate.add_argument("--port", default="auto")
    generate.add_argument("--pattern", default="palette_wave", choices=PATTERN_NAMES)
    generate.add_argument("--output", "-o", default="-", help="output path, or - for stdout")
    generate.set_defaults(func=_cmd_generate)

    run = subparsers.add_parser("run", help="run a show")
    run.add_argument("config")
    run.add_argument("--seconds", type=float, help="stop after this long (default: run until interrupted)")
    run.add_argument("--frames", type=int, default=0, help="stop after this many frames")
    run.add_argument("--dry-run", action="store_true", help="print frames instead of driving hardware")
    run.add_argument("--pattern", choices=PATTERN_NAMES, help="override the config's pattern")
    run.add_argument("--state", help="for a state machine pattern, the look to open on")
    run.add_argument("--brightness", type=float, help="override master brightness (0..1)")
    run.add_argument("--verbose", "-v", action="store_true", help="echo the executable's logs")
    _add_tempo_args(run)
    run.set_defaults(func=_cmd_run)

    viewer = subparsers.add_parser(
        "view", help="watch the rig in a window, laid out from the config"
    )
    viewer.add_argument("config")
    viewer.add_argument("--pattern", help="start on this pattern instead of the config's")
    viewer.add_argument("--state", help="for a state machine pattern, the look to open on")
    viewer.add_argument("--live", action="store_true",
                        help="also drive the real rig; without this nothing is put on the wire")
    viewer.add_argument("--emit-rate", type=float, default=30.0,
                        help="frames per second to draw (default 30)")
    _add_tempo_args(viewer)
    viewer.set_defaults(func=_cmd_view)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        return args.func(args)
    except KeyboardInterrupt:
        # the controller's context manager has already sent the dark frame
        print("\ninterrupted", file=sys.stderr)
        return 130
    except (ConfigError, ShowError, BinaryNotFoundError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
