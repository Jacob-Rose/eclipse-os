# eclipse-dmx

Runs eclipse-os patterns on a real DMX rig, from a desktop, through an Enttec
USB widget. Windows and Linux.

This is a replacement for QLC+ on a fixed install, not a layer on top of one.
There is no show-control stack in the middle: a config file describes the
patch, the executable renders frames and puts them on the wire, and a python
wrapper handles configuration and drives the running process.

```
config.json ──> eclipse-dmx ──> Enttec USB widget ──> DMX fixtures
                    ^
                    │  line protocol on stdin
              python wrapper
```

## Why it is built this way

The pattern code is the same code that runs on the microcontrollers. `ecore`,
`eanim` and `eio::HSVStrip` compile unchanged for the desktop — the only thing
that differs is what sits behind the framebuffer. On a relic that is a strip of
neopixels; here it is a DMX universe. Patterns do not know the difference, so a
look developed for one runs on the other.

The split between the executable and python is deliberate:

- **the executable** owns frame timing and the wire. A DMX rig wants a steady
  refresh, and that is not something to hand to a garbage collector.
- **python** owns configuration and decisions. Building a patch, validating it,
  and running a cue list are all much nicer in python, and none of them are on
  the frame deadline.

## Build

Two steps, and the first is once per machine.

**Windows**

```powershell
cd desktop
.\tools\setup-toolchain.ps1     # once: installs a pinned MinGW-w64 via winget
.\build.ps1
```

**Linux / macOS**

```sh
cd desktop
./tools/setup-toolchain.sh      # once: installs a compiler + cmake via apt/dnf/pacman/brew
./build.sh
```

The setup script records what it installed in `tools/toolchain.env{,.ps1}`,
and the build script reads that. So the build does not depend on what happens
to be on PATH in a given shell, and a fresh machine gets the same compiler as
the one this was verified on. Both scripts are safe to re-run — they detect an
existing toolchain and just record the path.

If you already have a toolchain you would rather use, skip setup and point the
build at it:

```powershell
$env:ECLIPSE_DMX_MINGW_BIN = "C:\path\to\mingw64\bin"    # windows
export ECLIPSE_DMX_CXX=/usr/bin/g++                       # linux/macos
```

The binary lands in `build/eclipse-dmx` or `build\eclipse-dmx.exe`. On MinGW it
is statically linked against the gcc runtime, so it can be copied to a show
laptop that has never seen a compiler.

Verified with GCC 16.1.0 (MinGW-w64 UCRT, POSIX threads) on Windows 11.

`.\build.ps1 -Clean` / `./build.sh --clean` blows away `build/` first.

## Check it works with no hardware

`--dry-run` swaps the widget for a console output that prints each frame's
channel values to stderr. The whole chain runs; only the wire is missing.

```sh
./build/eclipse-dmx --config config/example.json --dry-run --frames 5
```

You should see the first twelve channels of five frames. Four RGB fixtures at
channels 1, 4, 7 and 10 means twelve channels of colour, changing frame to
frame as the palette sweeps.

## Run it for real

```sh
./build/eclipse-dmx --config config/example.json
```

`"port": "auto"` picks the first attached serial device that looks like a
widget. If that guesses wrong — likely on Windows, where a COM port does not
say what is behind it — list them and be explicit:

```sh
./build/eclipse-dmx --list-ports
./build/eclipse-dmx --config config/example.json --port COM4
```

## Config

### fixture profiles — the short way

A rig is usually N of the same light. A profile states that model's channel
layout once, and one `fixtures` entry patches the whole bank:

```json
"fixtures": [
  { "profile": "uking_par36", "name": "par", "address": 1, "count": 10 }
]
```

That is ten U'King Par 36 in 8-channel mode at addresses 1, 9, 17 … 73, with
each fixture's dimmer driven and its strobe/mode/colour channels parked. See
`config/uking_par36_x10.json`.

With a profile, `address` is the fixture's **own DMX address** — the number set
on its display — not the red channel. `count` patches that many one footprint
apart; `spacing` overrides the stride if addresses were left with gaps.

`--list-profiles` prints the shipped ones:

```
uking_par36 (8ch)  1:dimmer=255  2:red  3:green  4:blue  5:park=0  6:park=0  7:park=0  8:park=0
rgb3        (3ch)  1:red  2:green  3:blue
rgb4_dimmer (4ch)  1:dimmer=255  2:red  3:green  4:blue
rgb7_par    (7ch)  1:red  2:green  3:blue  4:dimmer=255  5:park=0  6:park=0  7:park=0
```

Define your own in a `profiles` block, straight off the fixture's manual —
offsets are 1-based within the fixture, so a chart transcribes directly. A
profile defined here shadows a built-in of the same name:

```json
"profiles": {
  "my_par": {
    "footprint": 8,
    "dimmer": 1, "dimmer_value": 255,
    "red": 2, "green": 3, "blue": 4,
    "park": { "5": 0, "6": 0, "7": 0, "8": 0 }
  }
}
```

`park` is the part that matters most. A cheap par will sit dark, or strobe, or
run its own colour macro and ignore you entirely, until its mode channels are
pinned. Putting that in the profile solves it once per model instead of once
per rig.

### checking a patch

Before touching hardware, print what actually resolved:

```sh
eclipse-dmx --config config/uking_par36_x10.json --show-patch
```

Then confirm the physical order — on ten identical pars, the only way to know
that `par_7` is the seventh one on the truss is to light it alone and go look:

```sh
eclipse-dmx --config config/uking_par36_x10.json --pattern identify
```

`identify` lights one fixture at a time in white, in patch order. White because
a wrong channel order shows up as a colour cast rather than hiding behind a hue
that happens to look plausible. `pattern.speed` is fixtures per second.

### the long way

See `config/example.json` for a plain row of RGB pars, and
`config/example_7ch_pars.json` for spelling out every channel by hand. Without
a profile, `start_channel` is the **red** channel and `dimmer_channel` /
`static_channels` keys are absolute channel numbers.

```json
{
  "device":  { "type": "enttec_pro", "port": "auto", "fps": 40 },
  "master":  { "brightness": 1.0, "gamma": 2.2 },
  "pattern": { "name": "palette_wave", "speed": 0.25, "palette": "p_bluemagic" },
  "fixtures": [
    { "name": "par_left", "start_channel": 1, "channels": "rgb", "position": [0.0, 0.0] }
  ]
}
```

Channel numbers are 1-based, the way they read on a fixture's own display.
`//` comments are allowed.

### fixtures

| field | meaning |
| --- | --- |
| `name` | for logs and error messages |
| `start_channel` | address of the fixture's **red** channel |
| `channels` | order within the fixture: `rgb`, `grb`, `brg`, … |
| `dimmer_channel` | absolute channel of a master dimmer, `0` for none |
| `dimmer_value` | what to hold the dimmer at, default 255 |
| `static_channels` | absolute channel → fixed value, for strobe/mode channels |
| `position` | `[x, y]`, where the fixture is in the rig; drives spatial patterns |
| `brightness` | per-fixture trim, 0..1 |

`start_channel` points at red rather than at the fixture's own address, because
on a par with a dimmer in front of the colours those are not the same number.
`dimmer_channel` and the `static_channels` keys are absolute, so you never have
to work out an offset.

If a fixture stays dark on a real rig, it is almost always one of two things:
the master dimmer is not being driven (`dimmer_channel`), or the fixture is in
a mode that ignores its colour channels (`static_channels`).

### patterns

| name | what it does |
| --- | --- |
| `solid` | every fixture on `pattern.color` |
| `palette_wave` | the palette swept across the rig, driven by an eanim LFO |
| `rainbow` | hue ramp along the rig, rotating over time |
| `chase` | a lit fixture running the rig, driven by an eanim Saw |
| `pulse` | whole rig breathing on `pattern.color` |
| `identify` | one fixture at a time in white, for commissioning |
| `off` | dark |

`rainbow` is the one to reach for when commissioning: anything other than a
clean spectrum across the rig means a channel order is wrong.

`pattern.palette` takes either a built-in name from `kits/palettes.h` or an
explicit list like `["#ff0044", "#22ffcc"]`. `--list-palettes` names them all.

### gamma

`master.gamma` defaults to 2.2. LEDs are linear in duty cycle and eyes are
not, so without correction a fade spends most of its travel already looking
lit. Set it to `1.0` if your fixtures correct internally, or if you are
matching against something that sends linear values.

## Control protocol

The executable reads one command per line on stdin and replies `OK` or `ERR` on
stdout. Logs go to stderr, so the two are separable when it is being piped.

```
pattern <name>            speed <float>          width <float>
brightness <float>        master <float>         color <#rrggbb | h s v>
palette <name|#a,#b,...>  blackout <on|off>      status
quit
```

## Python wrapper

```sh
export PYTHONPATH=desktop/python      # or: pip install -e desktop/python
```

Describe a rig, validate it, run it:

```python
from eclipse_dmx import Config, ShowController

config = Config()
config.add_bank("uking_par36", count=10, address=1, name_prefix="par")

config.validate()          # raises on a channel collision or a bad patch

with ShowController(config) as show:
    show.set_pattern("chase")
    show.set_palette(["#ff2200", "#ffaa00"])
    show.set_master(0.6)
    show.wait(seconds=30)
```

`add_bank` takes a built-in profile name or a `FixtureProfile` you define:

```python
from eclipse_dmx import FixtureProfile

my_par = FixtureProfile(
    name="my_par", footprint=8,
    dimmer=1, red=2, green=3, blue=4,
    park={5: 0, 6: 0, 7: 0, 8: 0},
)
config.add_bank(my_par, count=6, address=81)
```

The controller writes the config to a temp file, starts the executable, waits
for `READY`, and speaks the protocol on its stdin. Leaving the `with` block
shuts the process down, which sends the rig one dark frame on the way out.

Validation in python is stricter than in the executable, on purpose. A channel
claimed twice is a warning at runtime — a half-repatched rig should still light
up — but an error when you are generating config from code, where it is
almost certainly a mistake.

There is a CLI for the common jobs:

```sh
python -m eclipse_dmx ports                    # what serial ports exist
python -m eclipse_dmx list                     # patterns, palettes and profiles
python -m eclipse_dmx validate my_rig.json     # check a patch, touch nothing
python -m eclipse_dmx patch my_rig.json        # print the resolved channel map
python -m eclipse_dmx generate --profile uking_par36 --count 10 -o my_rig.json
python -m eclipse_dmx run my_rig.json --dry-run --seconds 5
```

And a worked example with a cue list in `python/example_show.py`.

## Widgets

**Enttec DMX USB PRO** (`"type": "enttec_pro"`) is the one to use. It has
firmware that owns the DMX timing, so a frame is just a framed message over its
virtual COM port and the host being briefly busy does not disturb the output.
PRO Mk2 works on port 1.

**Enttec Open DMX USB** (`"type": "enttec_open"`) is supported because the
hardware is cheap and common, but it is the worse path. There is no firmware:
the host has to generate the DMX break and clock the frame out itself, so
output timing is at the mercy of the OS scheduler and you may see flicker under
load. On Linux it needs the FTDI VCP driver and 250000 baud, which is available
there but not on macOS.

**console** (`"type": "console"`, or `--dry-run`) prints frames instead of
sending them.

## What is not here yet

- **Art-Net / sACN.** Only the USB widgets. The `DmxOutput` interface is the
  place to add one.
- **Multiple universes.** One 512-channel universe.
- **Moving heads.** RGB colour fixtures only; pan/tilt has no representation in
  the config yet, though a profile's `park` will hold them somewhere sensible.
- **White / amber / UV channels.** Profiles model dimmer + RGB + parked
  channels. An RGBW fixture works, but its white channel can only be parked at
  a fixed value, not driven from the colour.
- **The state machine.** `esm` is not in the desktop build — it reaches into
  `eio::Relic` and calls an unqualified `clamp()` that only resolves on the
  Arduino side. Nothing about it is unportable, it is just not on the critical
  path. Cue sequencing lives in python for now.
- **Config hot reload.** Restart to change the patch. Look and brightness are
  live over the control protocol.

## Layout

```
desktop/
  include/edmx/    json, serial_port, dmx_output, fixture, config, pattern
  src/             implementations, plus main.cpp (the show runner)
  config/          example configs
  python/          the wrapper package
  tools/           toolchain setup, one script per platform
  CMakeLists.txt   builds only the slice of the library that is off-Arduino clean
```

The library changes that made this possible are in `src/lib/`: platform
includes are now behind `USE_ARDUINO`, and `ecore/platform_host.h` supplies the
few Arduino symbols the portable code leans on. The microcontroller build is
unaffected.
