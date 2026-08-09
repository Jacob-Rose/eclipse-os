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

## Watch it

Reading channel values off a terminal tells you the numbers changed. It does
not tell you the rig looks right. So there is a viewer:

```sh
python -m eclipse_dmx view config/uking_par36_x10.json --pattern obelisk_seasons
```

A window, one glowing disc per fixture, labelled with its name and address.
Nothing goes on the wire unless you add `--live`, so this is safe to run at a
desk with no rig attached.

```
[space] blackout   [n]/[p] pattern   [↑]/[↓] master   [←]/[→] speed   [q] quit
```

Two things about it are deliberate.

**The layout comes out of the config**, from the same `position` fields the
patterns are driven by — not from a hardcoded rig and not from the obelisk's
LED geometry. Point it at a different config and you get a picture of that rig.
A fixture with no `position` falls back to its patch order, so a config that
never mentions position still draws as the row it almost certainly is.

**The colours are read back out of the DMX universe**, not off the pattern. The
executable streams what it actually put in the frame, so a wrong patch shows up
as a wrong picture. A viewer fed from the pattern would cheerfully show a
beautiful rig while the fixtures sat dark, which is the one bug a viewer exists
to catch.

It needs tkinter, which ships with python on Windows and macOS. On Debian and
Ubuntu, `sudo apt install python3-tk`.

### the frame stream

The viewer is a client of a flag anything can use:

```sh
./build/eclipse-dmx --config config/example.json --dry-run --emit-frames
```

On stdout, alongside the normal protocol:

```
FIXTURES par_1 par_2 par_3 ...      once, at startup, in patch order
F e3c545 d94616 d55b1c ...          one per frame, rrggbb per fixture
```

`--emit-rate` caps it, 30 per second by default. It is a display rate, not the
DMX refresh — the rig still runs at `device.fps` whatever the viewer asks for.

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

That is ten U'King Par 36 in 7-channel mode at addresses 1, 8, 15 … 64, with
each fixture's dimmer driven and its strobe/mode/colour channels parked. See
`config/uking_par36_x10.json`.

With a profile, `address` is the fixture's **own DMX address** — the number set
on its display — not the red channel. `count` patches that many one footprint
apart; `spacing` overrides the stride if addresses were left with gaps.

`--list-profiles` prints the shipped ones:

```
uking_par36 (7ch)  1:dimmer=255  2:red  3:green  4:blue  5:park=0  6:park=0  7:park=0
rgb3        (3ch)  1:red  2:green  3:blue
rgb4_dimmer (4ch)  1:dimmer=255  2:red  3:green  4:blue
rgb7_par    (7ch)  1:red  2:green  3:blue  4:dimmer=255  5:park=0  6:park=0  7:park=0
```

#### zero-based or one-based

DMX512 numbers its 512 slots 1..512, and that is what this assumes. Plenty of
fixtures label their address dial 0..511 instead, so the first light reads as
`0` and the next as `8`. Rather than make you translate, say which numbering
the file uses:

```json
"addressing": "zero"
```

Every channel number in the file is then read that way — fixture addresses,
`start_channel`, `dimmer_channel`, and `static_channels` keys — and
`--show-patch` reports back in the same numbering, so what you read on screen
matches what is dialled on the fixture.

This changes nothing on the wire. A fixture at zero-based `0` and one at
one-based `1` are the same slot and produce byte-identical output. The default
is `"one"`.

One corner worth knowing: an *absent* `dimmer_channel` means the fixture has no
dimmer, in either numbering. An explicit `0` under zero-based addressing is a
real dimmer in the first slot.

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
| `obelisk_seasons` | the obelisk's four-seasons noise field |
| `obelisk_theater` | the obelisk's theatre chase |
| `obelisk_mono` | the obelisk's flat colour |

`rainbow` is the one to reach for when commissioning: anything other than a
clean spectrum across the rig means a channel order is wrong.

`--list-patterns` prints the live list, which is the authoritative one.

#### relic patterns

The `obelisk_*` entries are not reimplementations. They are
`src/relics/obelisk/state_obelisk.cpp` compiled for the host and run as-is.

A relic pattern is a `GeneratorHSV`: it reads a node's 2D coordinate and writes
a colour, and it has no idea whether the node is an LED on a strip or a par on
a truss. `edmx::GeneratorPattern` builds one `HSVStripNode_Mapped2D` per
fixture, hands them to the generator, and the look renders. Adding another relic
pattern is one source file in `ECLIPSE_RELIC_SOURCES` and one line in
`ensureBuiltinsRegistered()`.

What needs care is the *coordinate space*. Relic patterns were written against
a physical layout, and a noise field tuned for a 43-pixel strip reads as flat
colour if you hand it 0..1. So a rig's normalised positions are stretched
across a span:

```json
"pattern": { "coord_span_x": 8, "coord_span_y": 43 }
```

The defaults run a line of fixtures diagonally across the obelisk's own space:
8 wide (its four sides, two strips each, which is where the per-side palettes
in `obelisk_seasons` come from) and 43 tall (one strip, which is where its
noise gets its variation). A rig therefore picks up both the side palettes and
real spatial motion. Change them to take a different slice.

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
python -m eclipse_dmx view my_rig.json          # watch it in a window
```

And a worked example with a cue list in `python/example_show.py`.

## Widgets

**Enttec DMX USB PRO** (`"type": "enttec_pro"`) is the one to use. It has
firmware that owns the DMX timing, so a frame is just a framed message over its
virtual COM port and the host being briefly busy does not disturb the output.
PRO Mk2 works on port 1.

**Enttec Open DMX USB** (`"type": "enttec_open"`) covers the bare FTDI cables,
which is most of what "USB to DMX" means when it is cheap. There is no firmware:
the host generates the DMX break and clocks the frame out itself. On Linux it
needs the FTDI VCP driver and 250000 baud, which is available there but not on
macOS.

Three things make or break this path, all learned the hard way:

- **Flush before the break.** The break must not be asserted while the previous
  frame is still draining, or that frame is truncated and the break lands where
  no receiver expects it. `FlushFileBuffers` on Windows, `tcdrain` on posix.
- **Do not `sleep` for the break.** It is 92us, and a Windows sleep rounds up to
  the scheduler tick — 1ms at best, often 15. The problem is not that this is
  long but that it *varies*, and a receiver that cannot find a consistent break
  start reads every frame shifted. `serial_port.cpp` spins instead.
- **Send the whole frame at raised priority.** The break, the mark and the data
  are one indivisible thing to a receiver — a gap in the middle reads as a new
  break and the frame lands shifted. Nothing on the host is generating that
  timing except our thread, so if the scheduler preempts it mid-frame the rig
  blinks. `TimeCriticalSection` raises priority for the few milliseconds of the
  send and drops it straight back.

  This is worth the trouble: on this rig it took an unexplained flicker every
  few seconds — roughly one bad frame in two hundred — down to none over a
  minute of watching.

Get either wrong and the failure is not silence, it is a rig that looks steady
on a solid colour and strobes the moment anything animates — because a shifted
frame lands your colour data on the fixture's mode and strobe channels.

### which one do I have?

You cannot always tell from the USB descriptor. A widget that enumerates as a
plain `FT232R USB UART` with a stock EEPROM is a bare cable and wants
`enttec_open`; a real PRO programs its EEPROM to say `DMX USB PRO`. But clones
vary, so if in doubt try both:

```sh
eclipse-dmx --config my_rig.json --device enttec_open --pattern solid
eclipse-dmx --config my_rig.json --device enttec_pro   --pattern solid
```

`solid` is the right pattern for this: an unchanging frame, so anything other
than one steady colour means the frames are not arriving intact.

If QLC+ is installed, its Inputs/Outputs panel names the device it found, and
its `Fixtures/` folder holds `.qxf` definitions that are a far better source for
a channel map than most listings online.

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
