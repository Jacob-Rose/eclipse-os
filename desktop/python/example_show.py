#!/usr/bin/env python3
"""End-to-end example: describe a rig in python, run it, drive it live.

Run it against real hardware::

    python example_show.py

Or with no widget attached, to watch the DMX frames instead::

    python example_show.py --dry-run
"""

from __future__ import annotations

import argparse
import sys
import time

from eclipse_dmx import Config, Fixture, ShowController


def build_rig() -> Config:
    """Four RGB pars in a row, plus one 7-channel par with a master dimmer."""
    config = Config()

    config.device.type = "enttec_pro"
    config.device.port = "auto"
    config.device.fps = 40

    config.master.brightness = 0.9
    config.master.gamma = 2.2

    config.pattern.name = "palette_wave"
    config.pattern.speed = 0.3
    config.pattern.width = 2.0
    config.pattern.palette = "p_bluemagic"

    # a plain row at channels 1, 4, 7, 10
    config.add_rgb_bank(count=4, first_channel=1, name_prefix="par")

    # one cheap 7-channel par: dimmer at 13, colours at 14..16, strobe and
    # mode parked so it actually obeys the colour channels
    config.add_fixture(
        Fixture(
            name="wash",
            start_channel=14,
            channels="rgb",
            dimmer_channel=13,
            dimmer_value=255,
            static_channels={17: 0, 18: 0, 19: 0},
            position=[0.5, 1.0],
        )
    )

    return config


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="print frames instead of driving hardware")
    args = parser.parse_args()

    config = build_rig()

    # Catch patch mistakes here, before anything reaches the rig.
    for warning in config.validate():
        print(f"warning: {warning}", file=sys.stderr)

    print(config.to_json())
    print("---", file=sys.stderr)

    with ShowController(config, dry_run=args.dry_run, on_log=lambda line: print(line, file=sys.stderr)) as show:
        print(show.status(), file=sys.stderr)

        # A short cue list, driven entirely from python while the executable
        # keeps the universe refreshing underneath.
        cues = [
            ("palette wave, cool", lambda: show.set_palette(["#0044ff", "#00ffcc"])),
            ("faster",             lambda: show.set_speed(0.8)),
            ("chase",              lambda: show.set_pattern("chase")),
            ("warm chase",         lambda: show.set_palette(["#ff2200", "#ffaa00"])),
            ("pulse on red",       lambda: (show.set_pattern("pulse"), show.set_color("#ff0033"))),
            ("dim",                lambda: show.set_master(0.3)),
            ("rainbow, full",      lambda: (show.set_pattern("rainbow"), show.set_master(1.0))),
            ("blackout",           lambda: show.blackout(True)),
        ]

        for label, cue in cues:
            print(f"cue: {label}", file=sys.stderr)
            cue()
            time.sleep(3.0)

    return 0


if __name__ == "__main__":
    sys.exit(main())
