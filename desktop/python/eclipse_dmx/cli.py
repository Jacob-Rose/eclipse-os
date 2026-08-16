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
from .osc import DEFAULT_ADDRESS, DEFAULT_CONTROL
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


def _cmd_osc(args: argparse.Namespace) -> int:
    """Runs a show and sends one fixture's colour out as OSC.

    The bridge to a visualiser — Synesthesia, or anything else with an OSC
    colour control. See eclipse_dmx/osc.py for why this reads the frame stream
    rather than living in the executable.
    """
    from .osc import SynesthesiaLink

    if not args.test and not args.config:
        print("error: osc needs a config, or --test to send without a show",
              file=sys.stderr)
        return 1

    # The frame carries master.gamma already applied, for LEDs that need it.
    # A screen does not; see SynesthesiaLink.gamma. Read from the show rather
    # than assumed, so a config that turns gamma off does not get it undone.
    ungamma = args.ungamma
    if ungamma is None:
        ungamma = Config.load(args.config).master.gamma if args.config else 0.0

    try:
        link = SynesthesiaLink(
            args.address,
            args.control,
            separate=args.separate,
            gamma=0.0 if args.no_ungamma else ungamma,
        )
    except (OSError, ValueError) as error:
        # ValueError is a host:port that is not one; OSError is a name that
        # does not resolve or a route that does not exist. Between them they
        # are the only send-side failure that gets reported at all — see the
        # note at the end of osc.py — so they are worth a sentence each rather
        # than a traceback.
        print(f"error: cannot send to '{args.address}': {error}", file=sys.stderr)
        return 1

    with link:
        print(f"sending to {link.describe()}", file=sys.stderr)

        if args.test:
            return _osc_test(link, args)

        # Which fixture is the rig's colour. An index into the flat run of
        # fixtures across every device, which is exactly what a frame is; or
        # the first fixture of a named device, resolved once the executable has
        # announced its devices.
        chosen = [args.fixture]
        announced = [False]
        complained = [False]

        def frame(colors) -> None:
            if not announced[0]:
                announced[0] = True
                if args.device:
                    span = next((d for d in show.devices if d.name == args.device), None)
                    if span is None:
                        names = ", ".join(d.name for d in show.devices) or "none"
                        print(f"warning: no device '{args.device}' (have: {names}),"
                              f" sampling fixture {chosen[0]}", file=sys.stderr)
                    else:
                        chosen[0] = span.first
                index = chosen[0]
                name = show.fixture_names[index] if index < len(show.fixture_names) else "?"
                print(f"sampling fixture {index} ('{name}') of {len(colors)}",
                      file=sys.stderr)

            index = chosen[0]
            if index < len(colors):
                link.send_color(colors[index])
            elif not complained[0]:
                # Said once, and out loud, while the show is still running.
                # Otherwise the only sign is a count of zero at the end, which
                # is what a show producing no frames at all also looks like.
                complained[0] = True
                print(f"warning: fixture {index} is past the end of a "
                      f"{len(colors)}-fixture frame; nothing is being sent",
                      file=sys.stderr)

        # autostart=False so `show` is bound before any frame can arrive: the
        # callback above reads it, and the reader thread starts inside start().
        show = ShowController(
            args.config,
            executable=args.executable,
            dry_run=args.dry_run,
            midi=args.midi,
            bpm=args.bpm,
            on_frame=frame,
            emit_rate=args.rate,
            on_log=(lambda line: print(line, file=sys.stderr)) if args.verbose else None,
            autostart=False,
        )
        show.start()

        # The report is in a finally because without --seconds this runs until
        # Ctrl-C, which is the normal way to use it — and a diagnostic that
        # only prints on the exit nobody takes is not a diagnostic. main()
        # catches the KeyboardInterrupt after this has had its say.
        try:
            with show:
                for warning in show.warnings:
                    print(f"warning: {warning}", file=sys.stderr)
                for name, why in show.offline_devices:
                    print(f"warning: device '{name}' is offline: {why}", file=sys.stderr)

                # The cue matters here in a way it does not for `run`: what is
                # being sent is a *colour*, and a monochrome look sends grey.
                # mythos26 opens on beat_pulse, which is white on every hit for
                # good reasons of its own, and a scene keyed on grey has nothing
                # to key. So the same overrides `run` has, for the same reason
                # it has them - to open on the look you meant.
                if args.pattern:
                    show.set_pattern(args.pattern)
                if args.state:
                    show.set_state(args.state)

                print(f"running: {show.status()}", file=sys.stderr)
                code = show.wait(args.seconds)
        finally:
            _osc_report(link)

        return 0 if code is None else code


def _osc_test(link, args: argparse.Namespace) -> int:
    """Sweeps a hue at the control, with no show and no hardware involved.

    The first thing to run, and the one that answers the only question worth
    de-risking: is the app listening, on this port, at this address, in this
    message form. Everything downstream assumes the answer.
    """
    import colorsys
    import time

    seconds = args.seconds if args.seconds else 10.0
    period = 1.0 / max(args.rate, 1.0)
    deadline = time.monotonic() + seconds
    print(f"sweeping a hue for {seconds:.0f}s - the control should move",
          file=sys.stderr)

    try:
        while time.monotonic() < deadline:
            hue = (time.monotonic() * 0.25) % 1.0
            rgb = colorsys.hsv_to_rgb(hue, 1.0, 1.0)
            link.send_color([int(channel * 255) for channel in rgb])
            time.sleep(period)
    finally:
        _osc_report(link)

    return 0


def _osc_report(link) -> None:
    """What we can and cannot say about whether that worked.

    Carefully worded, because the honest answer is "we do not know". UDP is
    unacknowledged, and a closed port on loopback does not even come back as an
    error — so a clean run against nothing at all looks exactly like a clean run
    against a listening visualiser. Saying "88 sent, 0 dropped" and stopping
    would read as confirmation of the one thing this cannot confirm.
    """
    print(f"\n{link.sent} messages sent to {link.host}:{link.port}"
          + (f", {link.dropped} refused" if link.dropped else ""), file=sys.stderr)

    if link.dropped:
        print(f"  last error: {link.last_error}", file=sys.stderr)
    if not link.sent:
        print("  nothing was sent - either no frame arrived, or the fixture "
              "being sampled is not in it.", file=sys.stderr)
        return

    print(
        "\n  UDP is unacknowledged: sending is not evidence anything received it.\n"
        "  If the control did not move, in the order worth checking:\n"
        "    - is OSC *input* switched on in Synesthesia's settings? It ships\n"
        f"      off, and off looks exactly like every other failure here.\n"
        f"    - does that panel say port {link.port}? Both ends have to agree,\n"
        "      and neither will complain if they do not.\n"
        "    - OSC input is a Synesthesia Pro feature; on other licences the\n"
        "      fallback is MIDI, which this does not speak.\n"
        "    - does the running scene actually have a colour control? The\n"
        "      default address is positional - the *first* one, if there is one.\n"
        "    - if it has one and it still sits still, try --separate.",
        file=sys.stderr,
    )


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

    # The window and the visualiser off one show, rather than two copies of it
    # running side by side with their own beat clocks. See ViewerApp._send_osc.
    link = None
    if args.osc:
        from .osc import SynesthesiaLink

        try:
            link = SynesthesiaLink(
                args.osc,
                args.osc_control,
                gamma=Config.load(args.config).master.gamma,
            )
        except (OSError, ValueError) as error:
            print(f"error: cannot send to '{args.osc}': {error}", file=sys.stderr)
            return 1

        print(f"sending to {link.describe()}", file=sys.stderr)

    return view(
        args.config,
        executable=args.executable,
        pattern=args.pattern,
        state=args.state,
        live=args.live,
        emit_rate=args.emit_rate,
        midi=args.midi,
        bpm=args.bpm,
        osc=link,
        osc_device=args.osc_device,
        osc_fixture=args.osc_fixture,
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

    osc = subparsers.add_parser(
        "osc",
        help="run a show and send one fixture's colour out as OSC, for a visualiser",
        description="Sends the rig's colour to an OSC colour control - Synesthesia's, "
                    "by default. Start with --test, which needs no config and no "
                    "hardware and proves the app is listening.",
    )
    osc.add_argument("config", nargs="?", help="the show to run (not needed with --test)")
    osc.add_argument("--address", default=DEFAULT_ADDRESS,
                     help=f"where the visualiser listens (default {DEFAULT_ADDRESS})")
    osc.add_argument("--control", default=DEFAULT_CONTROL,
                     help=f"OSC address of the colour control (default {DEFAULT_CONTROL}); "
                          f"/controls/scene/<name> targets one scene's control by name")
    osc.add_argument("--fixture", type=int, default=0, metavar="N",
                     help="which fixture is the rig's colour, indexed across every "
                          "device in patch order (default 0)")
    osc.add_argument("--device", metavar="NAME",
                     help="sample that device's first fixture instead of --fixture")
    osc.add_argument("--pattern", choices=PATTERN_NAMES, help="override the config's pattern")
    osc.add_argument("--state", help="for a state machine pattern, the look to open on")
    osc.add_argument("--separate", action="store_true",
                     help="send r, g and b as three messages instead of one with "
                          "three floats - the fallback if the control does not move")
    osc.add_argument("--ungamma", type=float, metavar="G",
                     help="undo this gamma before sending (default: the config's "
                          "master.gamma, because a screen corrects again)")
    osc.add_argument("--no-ungamma", action="store_true",
                     help="send the frame's bytes as they are")
    osc.add_argument("--rate", type=float, default=30.0,
                     help="messages a second (default 30)")
    osc.add_argument("--seconds", type=float,
                     help="stop after this long (default: until interrupted; 10 with --test)")
    osc.add_argument("--test", action="store_true",
                     help="sweep a hue with no show running, to prove the link")
    osc.add_argument("--dry-run", action="store_true",
                     help="render without driving hardware; the OSC still goes out")
    osc.add_argument("--verbose", "-v", action="store_true", help="echo the executable's logs")
    _add_tempo_args(osc)
    osc.set_defaults(func=_cmd_osc)

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
    viewer.add_argument("--osc", nargs="?", const=DEFAULT_ADDRESS, metavar="HOST:PORT",
                        help="also send one fixture's colour out as OSC, for a "
                             f"visualiser (default {DEFAULT_ADDRESS})")
    viewer.add_argument("--osc-device", metavar="NAME",
                        help="sample that device's first fixture (default: fixture 0)")
    viewer.add_argument("--osc-fixture", type=int, default=0, metavar="N",
                        help="which fixture to send, indexed across every device")
    viewer.add_argument("--osc-control", default=DEFAULT_CONTROL,
                        help=f"OSC address of the colour control (default {DEFAULT_CONTROL})")
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
