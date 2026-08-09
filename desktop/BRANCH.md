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

# or just look at it: a window, one disc per fixture, no hardware, no wire
$env:PYTHONPATH = "python"
python -m eclipse_dmx view config\uking_par36_x10.json --pattern obelisk_seasons
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
| par_1 | 1 | | par_6 | 36 |
| par_2 | 8 | | par_7 | 43 |
| par_3 | 15 | | par_8 | 50 |
| par_4 | 22 | | par_9 | 57 |
| par_5 | 29 | | par_10 | 64 |

Highest channel used is 70, so there is room for 442 more on the universe.

The `uking_par36` profile encodes the chart you gave me, in 7-channel mode:

```
1 dimmer   2 red   3 green   4 blue   5 strobe   6 mode   7 colour
```

Channels 5–7 are parked at 0. **This is the part that usually decides whether a
cheap par works at all** — leave the mode channel floating and the fixture runs
its own colour macro and ignores you.

If a rig ever turns up whose fixtures number their address dial from 0 instead
of 1, put `"addressing": "zero"` at the top of its config and write the numbers
the way the fixtures show them. It changes how the *file* is read and nothing
else; the slots on the wire never move.

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
  python/          the wrapper package, and the viewer
  python/tests/    unittest suite, runnable with nothing installed
  tools/           toolchain setup, one script per platform
  readme.md        the real docs - config schema, protocol, patterns
  BRANCH.md        this file
```

### the layers

| layer | what it owns |
| --- | --- |
| `ecore` / `eanim` | colour and motion — unchanged, shared with the relics |
| `edmx::Pattern` | patterns, thin wrappers over `HSVPalette` + `LFO`/`Saw` |
| `edmx::GeneratorPattern` | runs any relic `GeneratorHSV` on a DMX rig |
| `edmx::FixtureMap` | HSV → RGB → DMX channels, gamma, dimmer, parked channels |
| `edmx::DmxOutput` | the wire: Enttec PRO, Enttec Open, or console |
| `main.cpp` | frame timing, the stdin control protocol, the frame stream |
| `python/` | configuration, validation, driving a running process |
| `python/viewer.py` | the window |

### relic patterns run here unmodified

This is the part worth understanding, because it is what the whole layering was
for. A relic pattern is a `GeneratorHSV`: it reads a node's 2D coordinate and
writes a colour. It does not know whether the node is an LED on a strip or a
par on a truss.

So `edmx::GeneratorPattern` builds one `HSVStripNode_Mapped2D` per fixture,
hands them to the generator, and the obelisk's own looks render on the rig.
`src/relics/obelisk/state_obelisk.cpp` is compiled into the host build directly
— not copied, not ported, not adapted. It needed no changes at all.

Adding another relic's patterns is one source file in `ECLIPSE_RELIC_SOURCES`
and one line in `ensureBuiltinsRegistered()`. There is no switch statement to
edit; patterns register themselves into a table.

The one thing that needs thought is the coordinate space. Relic patterns were
written against a physical layout, and a noise field tuned for a 43-pixel strip
reads as flat colour if you hand it 0..1. `pattern.coord_span_x` / `_y` stretch
the rig across that space, defaulting to the obelisk's own 8 × 43 — 8 being its
four sides at two strips each, which is where `obelisk_seasons` gets its
per-side palettes, and 43 being one strip, which is where its noise gets its
variation.

### the viewer

```sh
python -m eclipse_dmx view config/uking_par36_x10.json --pattern obelisk_seasons
```

A tkinter window, one glowing disc per fixture. Nothing reaches the wire
without `--live`, so it is safe at a desk.

Two decisions in it are load-bearing:

**The layout is read from the config**, off the same `position` fields that
place fixtures in the pattern's coordinate space — not from the obelisk's LED
geometry and nothing hardcoded. Point it at another rig and you get that rig.

**The colours are read back out of the DMX universe**, not off the pattern.
`main.cpp` streams what it actually put in the frame over `--emit-frames`. A
viewer fed from the pattern would happily show a beautiful rig while the patch
was wrong, which is the one bug a viewer exists to catch.

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
| *uncommitted* | relic patterns on the rig, zero-based addressing, the viewer, the test suite |

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

- **`eio/screen_drawer.{h,cpp}`** — `USE_SCREEN` is now `(USE_ARDUINO && 1)`
  and the guard in the `.cpp` is `#if`, not `#ifdef`. This is what lets
  `eio::Relic`, and therefore `esm`, compile for the host at all.
- **`esm/state_generic.cpp`** — see bug 4 below.

Five real bugs fixed along the way:

1. `get_random_int_in_range` returned the range *width*, not the drawn value.
2. `fp.h` declared `getFloat` `static` in a header, so every TU got an
   internal-linkage declaration with no definition behind it — calling it would
   not have linked. `fp.cpp` defined it at global scope, not in `efp`.
3. `fixtureHighestChannel` assumed three consecutive colour channels.
4. `state_generic.cpp` called an unqualified `clamp()`, which only resolved via
   the `using namespace std;` that `relic.h` happens to leak, and then only
   when something else had already pulled in `<algorithm>`. Now `std::clamp`.
5. `screen_drawer.cpp` guarded on `#ifdef USE_SCREEN`, which was always true —
   the macro is always *defined*; it is its *value* that says whether there is
   a screen. The Arduino build was compiling the screen path regardless.

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

And, for the relic patterns and the viewer:

- all three `obelisk_*` patterns render on the 10-par rig, with the four
  seasons palettes landing per side and the theatre chase alternating fixtures
- zero-based and one-based configs describing the same rig resolve to
  byte-identical channel use, in both python and the executable
- a zero-based config round-trips through python without shifting twice
- `--emit-rate` delivers the rate asked for (10, 20 and 30 all measured within
  a few percent, against a 40fps render loop)
- the viewer's glows stay inside the canvas and do not overlap, at window sizes
  from 640×320 to 1600×900
- blackout, master and pattern switching all reach the canvas
- frame lines do not accumulate in the controller's event list
- closing the viewer leaks no pipes and prints no Tcl error

On the real rig, through the widget on COM3:

- `solid` holds a steady, correct colour — frames arriving whole and aligned
- `identify` lights fixtures in patch order
- `obelisk_seasons` blends smoothly on actual pars — the relic's own pattern
  code driving DMX hardware, which is the thing this whole branch was for
- the viewer in `--live` mode drives the rig and mirrors it on screen at once

Rendering was measured independently of the wire, which is what finally located
the fault: across 173 frames of `obelisk_seasons`, per-fixture change between
consecutive frames had a **median of 0 and a maximum of 17** out of 255, with
no all-black frames. The look was always smooth; only the transport was not.

There is now a suite for all of this, which there was not before:

```sh
cd desktop
python -m unittest discover -s python/tests -v      # 35 tests
```

It needs nothing installed. Anything requiring the executable or a display
skips itself when there is not one.

Toolchain: GCC 16.1.0 (MinGW-w64 UCRT, POSIX threads) on Windows 11.

---

## On real hardware

**The wire works.** `solid`, `identify`, `obelisk_seasons` and the viewer in
`--live` mode all drive the rig correctly, through the widget on COM3.

The widget is a **bare FTDI cable**, `enttec_open`, not a PRO:

```
VID_0403 PID_6001,  bus-reported "FT232R USB UART",  serial BG00UIPI
```

A stock, unprogrammed EEPROM. There is no firmware in there; the host has to
generate the DMX break itself. QLC+ reads it the same way — its Inputs/Outputs
panel just shows `FT232R USB UART`, which is its DMX USB plugin falling back to
Open DMX because it did not recognise a PRO.

### the bug that cost the most time

**On Windows the break was being asserted while the previous frame was still
draining out of the driver.** The posix path had always called `tcdrain` first
and even said why in a comment; the Windows path had no `FlushFileBuffers`
beside it. So every frame was truncated and the break landed somewhere no
receiver expects. Compounding it, the break and mark-after-break were timed
with `sleep_for`, and a Windows sleep rounds up to the scheduler tick — 1ms at
best, often 15 — so the break was not merely long but *variable*, which is
worse: a receiver that cannot find a consistent break start reads every frame
shifted. Both are fixed in `serial_port.cpp`; the waits are spins now.

### the flicker after that

Once frames were landing, an occasional flicker remained: irregular, every few
seconds, roughly one bad frame in two hundred. That is the *other* half of the
same problem. The break, the mark and the frame are indivisible to a receiver —
a gap in the middle reads as a new break — and on a bare FTDI cable nothing is
generating that timing except our thread. When Windows preempts it mid-frame,
the rig blinks.

`TimeCriticalSection` in `serial_port.h` raises thread priority for the few
milliseconds of a send and drops it straight back. Scoped, not set once at
startup: a process sitting at time-critical priority for its whole life is a
bad neighbour, and the risky window is only milliseconds. That took the flicker
from every few seconds to none over a minute of watching.

It reduces the odds; it cannot remove them. Host-timed DMX is best-effort by
construction. **A widget with firmware — a real Enttec DMX USB PRO — owns the
timing in hardware and ends this class of problem outright**, and the
`enttec_pro` path is already written for one. Failing that, lowering
`device.fps` cuts the rate proportionally, since fewer frames means fewer
chances to corrupt one.

**What this looked like from the outside is worth remembering**, because it
sent me down two wrong paths. A shifted frame puts our red/green/blue onto the
fixture's **Strobe** and **Mode** channels, and on these pars `Mode` above 10
leaves Manual Control and starts an internal colour program (see the QLC+
definition, quoted below). So the rig ignored us and ran its own show. And the
tell that should have been decisive much earlier:

- a **constant** frame shifts to a constant wrong value, so the fixture sits in
  one steady macro and looks stable
- a **changing** frame makes the Mode channel jitter, so the fixture thrashes
  between programs and strobes

"Steady when solid, strobing when animated" is the signature of frame
misalignment, not of a pattern bug and not of bandwidth.

### wrong turns, recorded so they are not repeated

- **It is not a PRO.** PRO framing at 115200 produced sporadic response, which
  read as success and is not — those were accidental partial syncs. Open DMX
  looked dead only because our break was broken.
- **It was never bandwidth.** Trimming the universe and lowering fps changed
  nothing, because the frames were misaligned rather than late.
- **Do not raise `device.baud` for a PRO.** A PRO's MCU reads at a fixed
  115200; "faster" just hands it garbage. (Moot here — the open path is always
  250000 8N2 — but the config default is back to 115200 for that reason.)

### two things that will bite again

**Something else may already own the port.** QLC+ grabs its output on startup
and the only symptom is:

```
ERR output could not open COM3: Access is denied. (code 5)
```

One process owns the widget. Close the other one.

**`--device` exists now**, because choosing this from the command line is
exactly what commissioning needs:

```sh
eclipse-dmx --list-ports
eclipse-dmx --config config/uking_par36_x10.json --device enttec_open --port COM3 --pattern identify
```

If nothing lights, in likelihood order: something else holds the port, the
widget wants the other protocol, the fixtures are in sound/auto mode rather
than DMX, or they are not at the addresses above. `--dry-run` confirms the
frame content independently of the wire, and the viewer shows the same thing as
a picture.

### the fixtures, confirmed against QLC+

`C:\QLC+\Fixtures\UKing\UKing-Par-36.qxf` is the authoritative definition and
it matches our profile exactly:

```
1 Master Dimmer   2 Red   3 Green   4 Blue   5 Strobe   6 Mode   7 Color Selection
```

The `Mode` channel is the one to respect:

| value | behaviour |
| --- | --- |
| 0–10 | **Manual Control** — what we want, and what we park it at |
| 11–60 | colours selection |
| 61–110 | colours shade |
| 111–160 | colours pulse |
| 161–210 | colours transition |
| 211–255 | sound control |

Park it at 0 or the fixture runs its own program and ignores the rig entirely.
Same for `Strobe`, where 0–7 is "no strobe".

If a fixture ever misbehaves in a way that smells like it is running its own
show, that is the first thing to check — and if the channel map is ever in
doubt, the `.qxf` files under `C:\QLC+\Fixtures` are a better source than any
listing online.

---

## Known gaps

- **Art-Net / sACN** — USB widgets only. `DmxOutput` is where one would go.
- **Multiple universes** — one 512-channel universe.
- **White / amber / UV channels** — profiles do dimmer + RGB + parked channels.
  An RGBW fixture works, but its white channel can only be parked at a fixed
  value, not driven from the colour.
- **Moving heads** — no pan/tilt representation.
- **`esm` (the state machine)** — now *compiles* into the host build, and the
  unqualified `clamp()` that used to block it is fixed. But nothing drives it
  yet: cue sequencing still lives in python. Wiring a `StateMachine_GenericHSV`
  up to the show runner is the obvious next thing if you want looks to
  transition themselves the way they do on the relics.
- **Config hot reload** — restart to change the patch. Look and brightness are
  live over the control protocol.
- **The viewer draws discs, not beams.** Fixtures with a real position in space
  are drawn as a flat 2D scatter; there is no notion of where a light is
  pointing, so it cannot show you a stage wash. It answers "is each fixture
  doing the right thing", not "what will the room look like".
- **The viewer's layout is 2D only.** `position` takes x and y; a rig hung at
  different heights and depths flattens.

---

## Open questions for you

1. **Does the rig need cue sequencing?** Right now python drives cues
   imperatively (`example_show.py`). `esm` compiles into the host build now, so
   wiring it up would let looks transition themselves the way they do on the
   relics.
2. **Is one universe enough long-term?** Ten 7-channel fixtures is 70 channels,
   so there is a lot of headroom, but multi-universe is a real change if it is
   ever needed.
3. **Which relic patterns do you actually want on the rig?** Only the obelisk's
   three are registered. Anything else written as a `GeneratorHSV` is a one-line
   addition.
