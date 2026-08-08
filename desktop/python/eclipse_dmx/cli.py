"""Command line front end: ``python -m eclipse_dmx``.

Thin on purpose. This is the "does my rig work" tool, not a console.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List, Optional

from .binary import BinaryNotFoundError, find_executable
from .config import BUILTIN_PALETTES, PATTERN_NAMES, Config, ConfigError
from .controller import ShowController, ShowError
from .ports import list_ports


def _cmd_ports(args: argparse.Namespace) -> int:
    ports = list_ports(args.executable)
    if not ports:
        print("no serial ports found", file=sys.stderr)
        return 1
    for port in ports:
        print(port)
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
    """Writes a config for a plain row of RGB fixtures."""
    config = Config()
    config.device.type = args.device
    config.device.port = args.port
    config.pattern.name = args.pattern

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


def _cmd_run(args: argparse.Namespace) -> int:
    config_path = Path(args.config)

    def log(line: str) -> None:
        print(line, file=sys.stderr)

    show = ShowController(
        config_path,
        executable=args.executable,
        dry_run=args.dry_run,
        frames=args.frames,
        on_log=log if args.verbose else None,
    )

    with show:
        for warning in show.warnings:
            print(f"warning: {warning}", file=sys.stderr)

        if args.pattern:
            show.set_pattern(args.pattern)
        if args.brightness is not None:
            show.set_master(args.brightness)

        print(f"running: {show.status()}", file=sys.stderr)

        code = show.wait(args.seconds)
        return 0 if code is None else code


def _cmd_list(args: argparse.Namespace) -> int:
    print("patterns:")
    for name in PATTERN_NAMES:
        print(f"  {name}")
    print()
    print("palettes:")
    for name in BUILTIN_PALETTES:
        print(f"  {name}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="eclipse_dmx",
        description="configure and drive eclipse-dmx, the eclipse-os DMX renderer",
    )
    parser.add_argument("--executable", help="path to eclipse-dmx (default: the build output, then PATH)")

    subparsers = parser.add_subparsers(dest="command", required=True)

    ports = subparsers.add_parser("ports", help="list serial ports the executable can see")
    ports.set_defaults(func=_cmd_ports)

    listing = subparsers.add_parser("list", help="list available patterns and palettes")
    listing.set_defaults(func=_cmd_list)

    validate = subparsers.add_parser("validate", help="check a config without touching hardware")
    validate.add_argument("config")
    validate.add_argument("--allow-overlap", action="store_true",
                          help="treat channel collisions as warnings instead of errors")
    validate.set_defaults(func=_cmd_validate)

    generate = subparsers.add_parser("generate", help="write a config for a row of RGB fixtures")
    generate.add_argument("--count", type=int, required=True, help="how many fixtures")
    generate.add_argument("--first-channel", type=int, default=1)
    generate.add_argument("--channels-per-fixture", type=int, default=3,
                          help="address stride between fixtures (3 for a plain RGB par)")
    generate.add_argument("--order", default="rgb", help="channel order, e.g. rgb or grb")
    generate.add_argument("--device", default="enttec_pro", help="enttec_pro, enttec_open or console")
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
    run.add_argument("--brightness", type=float, help="override master brightness (0..1)")
    run.add_argument("--verbose", "-v", action="store_true", help="echo the executable's logs")
    run.set_defaults(func=_cmd_run)

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
