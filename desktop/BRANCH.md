# branch: `desktop-dmx`

What this branch adds, why, and how to pick it up cold.

Base: `master` at `f29a6dd`. Nothing on the microcontroller path changes
behaviour — the library edits are all behind `USE_ARDUINO`, and the Arduino
build compiles exactly the same translation units it did before.

---

## The short version

eclipse-os patterns now run on a real DMX rig from a desktop, through an Enttec
USB widget, on Windows and Linux. It replaces QLC+ for a fixed install rather
than sitting on top of one.

```
config.json ──> eclipse-dmx ──> Enttec USB widget ──> DMX fixtures
                    ^
                    │  line protocol on stdin
              python wrapper
```

The pattern code is the *same code* that runs on the relics. `ecore`, `eanim`
and `eio::HSVStrip` compile unchanged; only what sits behind the framebuffer
differs. On a relic that is a strip of neopixels, here it is a DMX universe.

Everything below is verified working on this machine except where explicitly
flagged.

---

## Try it in two minutes

```powershell
cd desktop
.\tools\setup-toolchain.ps1        # once per machine
.\build.ps1

# no hardware needed - prints DMX frames to stderr
.\build\eclipse-dmx.exe --config config\uking_par36_x10.json --dry-run --frames 5

# what the patch actually resolved to
.\build\eclipse-dmx.exe --config config\uking_par36_x10.json --show-patch
```

On Linux the same thing with `./tools/setup-toolchain.sh` and `./build.sh`.

---

## Your rig

`config/uking_par36_x10.json` is the ten U'King Par 36 in 8-channel mode, and
it is one entry:

```json
"fixtures": [
  { "profile": "uking_par36", "name": "par", "address": 1, "count": 10 }
]
```

Set the addresses on the fixtures to match:

| fixture | address | | fixture | address |
| --- | --- | --- | --- | --- |
| par_1 | 1 | | par_6 | 41 |
| par_2 | 9 | | par_7 | 49 |
| par_3 | 17 | | par_8 | 57 |
| par_4 | 25 | | par_9 | 65 |
| par_5 | 33 | | par_10 | 73 |

Highest channel used is 80, so there is room for 432 more on the universe.

The `uking_par36` profile encodes the chart you gave me:

```
1 dimmer   2 red   3 green   4 blue   5 strobe   6 mode   7 colour   8 speed
```

Channels 5–8 are parked at 0. **This is the part that usually decides whether a
cheap par works at all** — leave the mode channel floating and the fixture runs
its own colour macro and ignores you.

I could not find an authoritative U'King manual online (the ZQ01641 listing
says 7-channel, and the PDF I found would not extract), so **channel 8 is the
one thing I inferred rather than took from you** — you listed seven functions
for an 8-channel footprint. On these fixtures ch8 is speed, and 0 is correct
whether it is speed or unused. Worth a glance at your manual, and if it is
something else, it is one line in `desktop/src/fixture.cpp`.

### commissioning

Ten identical pars are indistinguishable from a config file, so:

```sh
eclipse-dmx --config config/uking_par36_x10.json --pattern identify
```

lights them one at a time in white, in patch order. Walk the rig, confirm
`par_7` is the seventh one on the truss. White on purpose — a wrong channel
order shows as a colour cast instead of hiding behind a plausible hue.

---

## What is where

```
desktop/
  include/edmx/    json, serial_port, dmx_output, fixture, config, pattern
  src/             implementations + main.cpp (the show runner)
  config/          example configs, including your rig
  python/          the wrapper package
  tools/           toolchain setup, one script per platform
  readme.md        the real docs - config schema, protocol, patterns
  BRANCH.md        this file
```

### the layers

| layer | what it owns |
| --- | --- |
| `ecore` / `eanim` | colour and motion — unchanged, shared with the relics |
| `edmx::Pattern` | patterns, thin wrappers over `HSVPalette` + `LFO`/`Saw` |
| `edmx::FixtureMap` | HSV → RGB → DMX channels, gamma, dimmer, parked channels |
| `edmx::DmxOutput` | the wire: Enttec PRO, Enttec Open, or console |
| `main.cpp` | frame timing, the stdin control protocol |
| `python/` | configuration, validation, driving a running process |

The split between the exe and python is deliberate: the exe owns frame timing
and the wire (a DMX rig wants a steady refresh, which is not something to hand
to a garbage collector), python owns configuration and decisions.

---

## Commits, in order

| commit | what |
| --- | --- |
| `aa600ad` | port `ecore`/`eio` off Arduino |
| `de8ff62` | `edmx`: serial, Enttec output, fixture patch, config |
| `02ca40a` | the executable, patterns, CMake |
| `e950a56` | python wrapper + readme |
| `f91889f` | reproducible toolchain, static link, `fp.h` fix |
| `fd4f2be` | fixture profiles, bulk patching, `identify` |

### library changes (`src/lib/`)

These are the only edits outside `desktop/`. All of them are behind
`USE_ARDUINO` or are strict fixes.

- **`ecore/core.h`, `ecore/fp.h`** — platform includes guarded. Arduino path
  untouched.
- **`ecore/platform_host.h`** (new) — supplies the handful of Arduino symbols
  the portable code leans on: `millis()`, a seeded `mt19937` behind the
  `get_random_*` helpers, and a `Serial` shim that routes logs to **stderr**
  (stdout is the control protocol).
- **`eio/hsv_strip.{h,cpp}`** — works with no pixel driver behind it. The
  constructor sizes the framebuffer and `getLength`/`getStripBrightness` read
  back from it instead of returning 0. Also parenthesised the `USING_NEOPIXEL`
  macro so `#if !USING_NEOPIXEL` means what it reads like.

Three real bugs fixed along the way:

1. `get_random_int_in_range` returned the range *width*, not the drawn value.
2. `fp.h` declared `getFloat` `static` in a header, so every TU got an
   internal-linkage declaration with no definition behind it — calling it would
   not have linked. `fp.cpp` defined it at global scope, not in `efp`.
3. `fixtureHighestChannel` assumed three consecutive colour channels.

---

## Verified

With no hardware attached:

- channel orders `rgb`/`grb`/`brg` land colour on the right channels
- dimmer and parked channels hold their values
- gamma 2.2 maps 128 → 56; gamma 1.0 passes it through
- per-fixture and master brightness scale correctly
- `rainbow` produces a clean spectrum across fixture positions
- `chase` tail fades correctly over the 7ch par config
- `identify` steps exactly one footprint per fixture
- **python and the executable resolve all 10 UKing addresses identically**
- python drives a live process: colour, master, blackout, pattern switch
- bad pattern/palette/number/command are rejected without killing the show
- missing port, missing config, malformed JSON and no fixtures all exit 1 with
  an actionable message
- the `.exe` runs with no MinGW on PATH (statically linked)

Toolchain: GCC 16.1.0 (MinGW-w64 UCRT, POSIX threads) on Windows 11.

---

## Not verified — needs your hardware

**The Enttec wire format has never touched a real widget.** The PRO framing is
written from the spec:

```
0x7E | label 6 | len lo | len hi | start code 0x00 + 512 channels | 0xE7
```

That is six lines in `desktop/src/dmx_output.cpp` and it is the one part of the
chain I could not exercise. Everything upstream of it — config, patch, pattern,
HSV→RGB, gamma, the universe buffer — is verified.

First thing to try when the widget is in front of you:

```sh
eclipse-dmx --list-ports
eclipse-dmx --config config/uking_par36_x10.json --port COM4 --pattern identify
```

If nothing lights, in likelihood order: wrong COM port, the widget wants a
different baud, or the fixtures are not actually at the addresses above. Add
`--dry-run` to confirm the frame content is right independent of the wire.

---

## Known gaps

- **Art-Net / sACN** — USB widgets only. `DmxOutput` is where one would go.
- **Multiple universes** — one 512-channel universe.
- **White / amber / UV channels** — profiles do dimmer + RGB + parked channels.
  An RGBW fixture works, but its white channel can only be parked at a fixed
  value, not driven from the colour.
- **Moving heads** — no pan/tilt representation.
- **`esm` (the state machine)** — not in the desktop build. It reaches into
  `eio::Relic` and calls an unqualified `clamp()` that only resolves on the
  Arduino side. Nothing about it is unportable; it just is not on the critical
  path, and cue sequencing lives in python for now. This is the obvious next
  thing if you want looks to sequence themselves.
- **`eanim/noise.cpp`** — FastNoiseLite is portable, just not needed until a
  pattern wants noise.
- **Config hot reload** — restart to change the patch. Look and brightness are
  live over the control protocol.
- **No automated test suite in-repo.** The verification above was done with
  throwaway scripts. If this is going to keep growing, that is worth fixing.

---

## Open questions for you

1. **Channel 8 on the Par 36** — see above. Worth a look at the manual.
2. **Does the rig need cue sequencing?** Right now python drives cues
   imperatively (`example_show.py`). Porting `esm` would let looks transition
   themselves the way they do on the relics.
3. **Is one universe enough long-term?** Ten 8-channel fixtures is 80 channels,
   so there is a lot of headroom, but multi-universe is a real change if it is
   ever needed.
