# branch: `desktop-dmx`

What this branch adds, why, and how to pick it up cold.

Base: `master` at `f29a6dd`.

For most of this branch nothing on the microcontroller path changed behaviour —
the library edits were all behind `USE_ARDUINO`, and the Arduino build compiled
exactly the same translation units it did before. **That stopped being true with
the USB link.** The relic now reads its cable every tick, `eclipse-os.ino` picks
which sculpture it is building for, and `RelicCore::preTick` no longer hands the
first frame the board's entire uptime as a delta. See
[over usb](#over-usb) and the note on the first frame below.

The sketch itself is **not compiled here** — there is no Arduino toolchain on
this machine. What is compiled and tested is everything under it, including
`ObeliskCore`; see [the firmware is tested without being
flashed](#the-firmware-is-tested-without-being-flashed).

---

## The short version

eclipse-os patterns now run on a real DMX rig from a desktop, through an Enttec
USB widget, on Windows and Linux. It replaces QLC+ for a fixed install rather
than sitting on top of one.

```
config.json ──> eclipse-dmx ──> Enttec USB widget ──> DMX fixtures
                  ^   ^
   MIDI tempo in ─┘   │  line protocol on stdin
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

# the show, pulsing on its own internal 128bpm - no DJ software, no MIDI
.\build\eclipse-dmx.exe --config config\mythos26.json --dry-run --bpm 128 --frames 80

# what the patch actually resolved to
.\build\eclipse-dmx.exe --config config\uking_par36_x10.json --show-patch

# or just look at it: a window, one disc per fixture, no hardware, no wire
$env:PYTHONPATH = "python"
python -m eclipse_dmx view config\uking_par36_x10.json --pattern obelisk_seasons

# and the obelisk itself - 344 pixels, its own geometry, patterns rendered
# on the shape they were written for
python -m eclipse_dmx view config\obelisk.json --pattern obelisk_seasons

# the link to a real sculpture, checked without one
.\build\eclipse-dmx.exe --link-selftest
.\build\eclipse-dmx.exe --probe-relics

# and with one plugged in and flashed
python -m eclipse_dmx view config\obelisk_usb.json --live
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
  include/edmx/    json, serial_port, dmx_output, fixture, config, pattern,
                   state_machine, beat_clock, midi_input, mythos26
                   (the tuning surface itself is ecore::PropertyBag, in src/lib)
  src/             implementations + main.cpp (the show runner)
  config/          example configs, your rig, the show, and the obelisk itself
  python/          the wrapper package, and the viewer
  python/tests/    unittest suite, runnable with nothing installed
  tools/           toolchain setup, one script per platform
  readme.md        the real docs - config schema, protocol, patterns, MIDI
  BRANCH.md        this file
```

### the layers

| layer | what it owns |
| --- | --- |
| `ecore` / `eanim` | colour and motion — unchanged, shared with the relics |
| `edmx::Pattern` | patterns, thin wrappers over `HSVPalette` + `LFO`/`Saw` |
| `edmx::GeneratorPattern` | runs any relic `GeneratorHSV` on a DMX rig |
| `edmx::StateMachinePattern` | runs a whole `esm` machine, with its cross-fades |
| `edmx::BeatClock` | where the beat is, and how fast |
| `edmx::MidiInput` | tempo in, off winmm or an ALSA rawmidi device |
| `edmx::FixtureMap` | HSV → RGB → channels, gamma, dimmer, parked channels |
| `edmx::DmxOutput` | the wire: Enttec PRO, Enttec Open, a relic on USB, console, or preview |
| `elink` | the frame format between a desk and a relic — shared with the firmware |
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

### mythos26 runs the other way round

`mythos26` is the first thing here that is *not* a relic look on a rig. It is
written for the rig, in `src/mythos26.cpp`, and two things follow.

Its coordinate space is the rig itself — `coord.y` runs 0..1 from the first
fixture to the last, with none of the stretching a relic look needs, because
there is no physical object it was tuned against.

And it reads the beat. `edmx::BeatClock` is one number the whole process agrees
on — how far through the current beat we are — fed by `edmx::MidiInput` from
whatever the DJ software is sending. Two properties are deliberate: it
*predicts*, because beats arrive twice a second and frames render forty times a
second, and it *free-runs*, because a rig that drifts out of time is a much
better failure than one that goes dark when the link drops.

Seven states, of which four are written:

| state | what it does |
| --- | --- |
| `beat_pulse` | the whole rig swells white on the beat, on an automation curve |
| `vu_pulse` | the same flash on every *second* beat, over a red backdrop that follows the track's loudness |
| `tv_static_mono` | every fixture a new grey, every frame |
| `tv_static` | every fixture a new colour, every frame |
| `slot_5`–`slot_7` | placeholders |

How often the beat-driven looks fire is a desk control, not a config one:
`beat div 1|2|4`, or **on 1 / on 2 / on 4** in the viewer. It overrides every
look at once, because a choice made at the desk should survive a cue change;
`beat div 0` ("auto") hands each look back its own default, which is 1 for
`beat_pulse` and 2 for `vu_pulse`. What it divides is the beat *count*, not the
tempo — dividing the tempo would stretch the envelope and the hit would go soft.

The static looks hash `(frame, fixture index)` rather than keeping a random
generator, which lets `render()` stay const and stateless and makes any given
frame reproducible.

### the envelope is an automation curve now

`beat_pulse` no longer owns its shape. `eanim::AutomationCurve` is a keyframe
curve — time, value, and an easing per segment — and
`eanim::AutomationCurveTrigger` plays one from an impulse and exposes the result
as a `FloatAttribute`. The beat is the impulse; the pulse is one three-key curve.

It lives in `src/lib/eanim/` rather than in `desktop/`, so the relics get it as
well: fixed capacity, no allocation, nothing host-only in it.

The part that needed a decision is what a second impulse does to a pass that is
still running, because the envelope is now longer than a beat. `RetriggerMode`
names the three answers: `Restart` (reset the timeline, which steps the output
back down — right when the curve fits in the gap), `RestartHold` (reset the
timeline but hold the output where it was until the new rise passes it, which is
what `beat_pulse` uses and why the rig swells instead of cutting to black), and
`Overlap` (each impulse gets its own voice, combined by `Max` or `Sum`, four
deep and then the oldest is stolen).

The peak-across-a-frame read moved down into `AutomationCurve::peak`, so it is
now a property of any curve rather than a hand-rolled special case in one look.

### the loudness backdrop, and getting it wrong twice

`vu_pulse` is a white flash over a red wash that tracks how loud the track is.
Worth recording how that went, because both wrong answers were plausible.

**First wrong answer: VU ballistics.** Fast attack, limited release — how a
meter is *drawn*. Because it snaps upward it keeps every transient it is
supposed to be removing, so the wash still moved on every kick. What a backdrop
needs is a *symmetric* low-pass, equally slow in both directions.

**Second wrong answer, and the real one: the wrong signal.** It was reading
note 64, "VU mono current" — the instantaneous level, resent every 40ms. That
peaks on every kick by construction, so no amount of smoothing downstream turns
it into a steady wash. Note 68, "VU mono average fit", is the same level over a
two-second window: the loudness of the *track*.

So all three useful signals are now read and kept apart, and a look picks one by
name rather than by note number:

| `VuSource` | note | what it is |
| --- | --- | --- |
| `Instant` | 64 | the level now — peaks on every kick |
| `Average` | 68 | ~2s average — what `vu_pulse` uses |
| `Meter` | 69 | a meter bar, quantised |

The two layers composite by *desaturating*, not adding: at full flash the red has
become white. Adding white to red gives pink.

**Mixxx sends notes, not beat clock**, which is the thing worth knowing before
touching any of this. Its MIDI-for-light mapping puts the beat on note 50, the
tempo on note 52 as `velocity + 50`, and VU meters on notes 64 and up, many per
second. So "any note-on is a beat" — the obvious default — would strobe the rig
rather than pulse it, and the tempo is better read off note 52 than measured off
intervals. Both are the defaults; see the table in `include/edmx/midi_input.h`.

This part is now **confirmed against a real Mixxx** over loopMIDI, which it was
not when the note map was first written.

`config/mythos26_mixxx.json` is the load-in config with all of that named
outright, and `readme.md` has the loopMIDI/Mixxx setup. `config/mythos26.json`
stays the bench version: `auto`, internal tempo, no assumptions about what is
plugged in.

### the DJ controller must never be the tempo source

Worth stating separately because it is the failure that would look like a bug in
the show. A controller is a MIDI input, is frequently the *only* MIDI input, and
sends notes from every pad, jog and transport button. It is simultaneously what
`"auto"` reaches for and the last thing that should move the beat — a hand on
the S2 mid-set would drag the whole rig off the music.

Two defences. Naming the port is the real one: a MIDI input is a separate
stream, so once we are on the loopMIDI cable the controller's messages are not
something we filter, they are something we never see. `midi.ignore` is the
backstop for when the cable is missing at startup and `auto` would fall through
to whatever is left — with everything ignored it refuses, says so, and free-runs
rather than quietly following a pad. An explicitly named port still wins over
the list, because naming it means you meant it.

### verifying it without Mixxx

Two tools, because "the notes are what the wiki says" was the one assumption
left in the whole path.

`python -m eclipse_dmx midi-watch <config>` listens on the cable, prints every
message, and says which of them we are reading as the beat. It forces a dry run,
so a diagnostic cannot flash the rig. That covers *which notes arrive*.

`eclipse-dmx --midi-selftest` covers everything downstream of that: it replays a
synthesised Mixxx stream, a bare clock stream, a source sending both at once, and
a raw byte stream with running status and an interleaved realtime byte — all in
synthetic time, because `handleMessage` takes its timestamp rather than reading a
clock. It found a real defect on first run: **clock tempo was a whole bpm out for
the first thirty beats**, because it was averaging beat-to-beat gaps when 24
ticks a beat were available. It now takes the tick 24 ago, which was one beat ago
by definition, and locks exactly after one beat.

### looks hand out their own knobs

A look's numbers are worth turning at the desk rather than recompiling for, so
`ecore::PropertyBag` and a `reflect()` virtual on `eanim::GeneratorHSV` let one
hand out the ones that matter — one line each:

```cpp
bag.add("floor", floorLevel, 0.0f, 1.0f);
bag.add("monochrome", monochrome);
bag.add("attack", attackSeconds, 0.0f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
```

`add` takes the member itself rather than a copy or a setter, so the look keeps
reading its own field and nothing is plumbed through. The optional callback is
for a value something else is derived from, which is the envelope's case exactly:
it keeps a built curve, not the two numbers behind it.

Floats and bools only. A knob is one or the other; anything richer is config.

Three decisions in it are worth keeping:

**The set belongs to the look, not the pattern.** `StateMachinePattern::reflect`
hands out the *showing* look's properties, and a bag is gathered fresh on every
call rather than cached — it holds pointers into whatever filled it, and on a
cue change that object is gone. `main.cpp` re-announces on every pattern and
state change, so a UI follows the show without polling.

**A value out of range is clamped, not refused.** The range is what a slider
spans, and a number typed slightly past it is a request for the end of the
slider rather than a mistake.

**The echo goes out before the OK.** A client that waits on the reply and then
reads the value would otherwise race the line telling it what the value became.
Emitted the other way round that test passes about half the time, which is the
worst kind of protocol bug to own.

Live only, deliberately: `params dump` prints the set as a line of JSON, and
nothing is written to a config file behind you. What gets kept is a paste, not a
side effect of turning a knob.

### the obelisk is a device now, not just a source of looks

Everything above runs a relic's *look* on somebody else's rig.
`config/obelisk.json` is the other direction: the sculpture itself, patched as a
device you can make patterns for and watch.

```sh
python -m eclipse_dmx view config/obelisk.json --pattern obelisk_seasons
```

344 pixels — four sides, two vertical strips each, 43 tall. Nothing on a wire;
that is the next section.

Two assumptions had to come out, and both were load-bearing in a way that is
worth recording, because each had a plausible wrong answer.

**The 512-slot limit was on the wrong object.** 344 pixels at three channels is
1032, twice a universe. The obvious reading is that the obelisk does not fit and
needs a parallel pixel path — a second buffer type, a second output interface, a
second render loop. It does not. A DMX universe is 512 because *DMX* is; the
buffer is just channels, and a pixel rig's is as long as its patch. So
`DmxUniverse` sizes at runtime, the limit moved onto the outputs that actually
speak DMX, and `FixtureMap`, `--show-patch`, the frame stream and the viewer all
work on the obelisk unchanged. Patch the same file to `enttec_open` and the
overflow is an error again, which is the check that says the limit is now in the
right place.

**A position was an ordering, and it had to become a coordinate.** A truss is
one-dimensional: a single 0..1 place along it says everything, and `CoordFrame`
stretches that across whatever space the look wants. The obelisk is not a line.
`obelisk_seasons` reads x as *which side* and y as *how far up* — that is where
its four palettes come from — and collapsing that to one scalar gives every side
the same colour. Which looks completely plausible on a rig of pars, and is the
wrong picture.

So `coord_space: "literal"` says the `position` fields are already in the
pattern's space and reach it untouched, via `PatternContext::nodeCoords`. The
default is unchanged. Same shape of decision as `addressing`: it changes what the
numbers in the *file* mean and nothing else.

The geometry is eight config entries, one per strip, because `position_step`
lets one entry describe a run rather than a point. Those eight lines are the
eight `GenerateAxisRow` calls in `src/relics/obelisk/obelisk.cpp` — same order,
same numbers, including the firmware's own off-by-one where the down strips run
43..1 against the up strips' 0..42. They are meant to be read side by side.

The viewer switches to bare pixels above 64 nodes. That is not a style choice: a
glow is nine canvas items and tk recolours them one at a time from python, so
344 fixtures would be 3096 `itemconfig` calls per repaint. It also skips
repainting a frame already on screen, which the 60Hz pump over a 30fps stream
makes half of them.

### over usb

A laptop takes a relic over for a show and hands it back.

```sh
eclipse-dmx --probe-relics
python -m eclipse_dmx view config/obelisk_usb.json --live
```

Two modes, and they are not alternatives: a **cue** is `state theater` and the
relic renders its own looks; a **pixel** frame is 1044 bytes at 30fps and puts
*any* desk look on the sculpture. `link cue` / `link pixels` switch live, and
cues are sent in either mode because arming the fallback is exactly what you
want set before a stream drops.

**The takeover ends by itself.** Half a second without a pixel frame and the
relic takes its own back. Not a design flourish — a pulled USB cable should cost
a look, not black out a sculpture in front of a room. `link release` and closing
the show both end it immediately instead, because a set that ends should end
rather than fade out on a timeout.

**One format, both ends.** `src/lib/elink/` is compiled into the firmware *and*
into this, exactly like the patterns, so there is no second definition of the
wire to drift. Magic, type, length, payload, CRC — with a resync scan, because a
relic gets plugged into a running desk and the first bytes it ever sees are the
tail of something. The CRC is there because the failure it catches is silent: a
corrupt pixel frame is not an error anyone sees, it is one wrong-coloured frame
that reads as a glitch in the look rather than as a bad cable.

**Finding it is a handshake, not a guess.** `"port": "auto"` sends a Hello to
every port and takes what answers by name. Guessing from the port name cannot
work here — a Pico is USB CDC and the DMX widget is FTDI, and on Windows both
are `COMn` with a description naming neither; this machine's widget calls itself
`\Device\VCP0`. A wrong guess means a show quietly driving something that
ignores it.

**Bandwidth was never the problem, and the obvious number is a red herring.**
`Serial.begin()` reads like a baud ceiling and is not one: on an RP2040 `Serial`
is USB CDC, where the rate is a value the host sets and neither end obeys. 31
KB/s is about 3% of what the link carries. The ceiling is the sculpture — 344
WS2812s are 10.3ms of `show()` against a 30ms loop — which is why the config says
30fps and not 40.

Three things that would have bitten, all now closed:

- **`DEPLOYMENT` disabled serial input.** `USE_SERIAL_INPUT` was
  `1 && !DEPLOYMENT`, so the build that goes on a sculpture for a show was
  exactly the build with no way in. `USE_RELIC_LINK` is its own flag, on in both.
- **Gamma, exactly once.** A streamed frame is already RGB and goes through
  `HSVStrip::setPixelRGB` straight to the driver, skipping the `gamma32` the
  sculpture applies to its own looks. Through `strip_HSV` it would be corrected
  twice and everything below mid-brightness would crush toward black — quiet
  failure, which is why it got its own door rather than a flag on `setHSV`.
- **`Serial` may be taken.** `USE_SERIAL_MQTT` puts a line-based bridge on that
  port; the sketch now refuses to compile with both on.

**The relic stays dimmer than the picture, on purpose.**
`setGlobalBrightness(EBrightness::HIGH)` is 63/255 and the link does not bypass
it: that is a current limit, and 344 pixels at full white is about 20A. A Hello
reports it so it is at least visible rather than mysterious.

### the firmware is tested without being flashed

`--link-selftest` is 41 checks and the last block is the important one: it
constructs a **real `ObeliskCore`** — its geometry, its state machine,
`RelicCore::runTick` and the gating inside it — and drives it through a loopback
transport. That is the code the Pico executes, compiled with `USE_ARDUINO=0`.

This is what `eclipse_relic_firmware` in the CMakeLists is for. The rule
everywhere else is that patterns are shared and device layers are not, and that
still holds — nothing in the show runner constructs an `ObeliskCore`. But it is
firmware, the Arduino toolchain is the only thing that normally compiles it, and
shipping a changed `ObeliskCore` unbuilt is not a position to be in when the next
step is reflashing a sculpture.

It earned itself immediately: `ObeliskCore::handleCommand` was calling `strcmp`
with no `<cstring>` in sight, resolving only through whatever Arduino.h drags in.
And it found the first-frame bug below.

What it cannot cover is what only exists on the device: the USB stack, the
WS2812 write, and every question about timing.

### the first frame was the whole uptime

`RelicCore::preTick` computed its delta against a default-constructed
`steady_clock::time_point`, which is that clock's epoch. So the *first* frame
after boot got a delta of however long the board had been powered, and every
animation on every relic jumped that far in one step before the first pixel was
lit. On a Pico the setup delays make it about five seconds; on a host it is
hours.

Fixed, and it is a real behaviour change on the microcontroller — the only one
on this branch.

### the viewer

```sh
python -m eclipse_dmx view config/uking_par36_x10.json --pattern obelisk_seasons
python -m eclipse_dmx view config/mythos26.json --bpm 128
```

A tkinter window, one glowing disc per fixture. Nothing reaches the wire
without `--live`, so it is safe at a desk.

Under the picture is a split — the cue list on the left, the running look's
knobs on the right. Two different questions: the left is a fixed table you learn
the shape of, the right changes on every cue. In one column the buttons moved
whenever a look with more properties came up.

Three decisions in it are load-bearing:

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
| `6115309` | relic patterns on the rig, zero-based addressing, the viewer |
| `6aeb62c` | the jacket's state machine on the rig, with UI buttons |
| `32e3ea6` | DMX frames sent at raised priority |
| `71a5a07` | `mythos26`, the beat clock, MIDI tempo in |
| `5e7ed8d` | `vu_pulse`, the static looks, beat division, the master slider |
| `c260b13` | `eanim::AutomationCurve`, and `beat_pulse` rebuilt on it |
| `e32c0d9` | `start.bat` |
| `e9d96d0` | per-look tunable properties, and the viewer's split |
| `ab92d49` | the obelisk as a device, and the USB link designed |

### library changes (`src/lib/`)

Up to the USB link these were the only edits outside `desktop/`, all of them
behind `USE_ARDUINO` or strict fixes. The link added more, and they are *not* all
inert on the microcontroller:

- **`elink/`** (new) — `frame.{h,cpp}` is the wire format and `relic_link.{h,cpp}`
  is the relic's end of it. Portable: no Arduino, no allocation on the hot path,
  a byte at a time in and a buffer out. `serial_transport.h` is the only
  Arduino-only file, and it is eleven lines.
- **`eio/hsv_strip.{h,cpp}`** — `setPixelRGB` writes a pixel straight to the
  driver with no HSV step and no gamma, for a frame that arrived already
  rendered. On a host it lands in a buffer a test can read.
- **`eio/relic.{h,cpp}`** — `RelicCore` owns a `RelicLink`. `runTick` reads the
  cable, drains cues into `handleCommand`, and calls `tickWhileLinked` instead of
  `tick` while the desk owns the pixels. **And `preTick` no longer gives the
  first frame the board's whole uptime as a delta** — see below.
- **`relics/obelisk/obelisk.{h,cpp}`** — `handleCommand` takes `state seasons |
  theater | mono` and `states`, so cue mode has looks to name. Its `strcmp` is
  now a `std::string` comparison; it never included `<cstring>`.

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

And, for mythos26 and the beat clock:

- `beat_pulse` reaches **full 255 on every beat**, not just the ones a frame
  happens to land on — see the peak-hold note in `mythos26.cpp`
- it is white on every hit: `r == g == b` on every fixture of every peak frame,
  which is also a channel-order check
- the whole rig hits together, and goes fully dark between beats
- doubling the tempo doubles the pulses in the same wall time
- a tap relights the rig within a frame, mid-gap
- with free-run off and nothing driving it, the rig settles dark rather than
  holding a level
- all seven states are announced in table order, and the six placeholders render
  visibly and differently from each other
- `BEAT` lines do not accumulate in the controller's event list, the same way
  frame lines do not
- a bad tempo, and a MIDI port that is not there, are both rejected without
  killing the show
- the viewer's tempo readout, its tap/bpm/division buttons and the master
  brightness slider all take, and the slider and the arrow keys stay in step
- `beat_pulse` opens on every beat and `vu_pulse` on twos, without anyone
  selecting anything; `on 1` and `on 4` override both, `auto` hands them back
- `tv_static_mono` is grey on every fixture of every frame, `tv_static` is not,
  fixtures differ from each other, and consecutive frames differ
- `--midi-selftest`: 23 checks over a synthesised Mixxx stream, a bare clock
  stream, both at once, the three loudness signals interleaved, and the raw
  byte parser — all in synthetic time
- with the controller as the only MIDI input, `auto` refuses it, warns, and the
  rig still lights and free-runs
- 50 new tests, none of which need a MIDI device: `--midi ""` keeps the suite
  off whatever happens to be plugged into the machine running it

And, for the USB link — all of it without a sculpture attached, over
`--link-selftest`'s loopback:

- a clean frame lands byte for byte, and leaves `strip_HSV` alone
- garbage in front of a frame is discarded and the frame still arrives: a stray
  magic byte, a doubled one, and the tail of a previous frame
- one flipped bit is caught by the CRC, and the strip is not touched
- an absurd length does not wedge the reader waiting for bytes that will never
  come; it recovers on the very next frame
- a byte-at-a-time trickle is identical to a burst
- a frame sized for a longer relic lights what fits rather than being refused
- a frame for a strip that does not exist is counted, and still counts as a
  takeover — going dark would hide the mistake
- the holdover expires in synthetic time, and `Release` ends it at once
- commands arrive as text, and typing into a serial monitor still becomes one
- the obelisk's own frame is 1044 bytes and all 1032 pixel bytes survive it
- and on a **real `ObeliskCore`**: it runs its own look unlinked, the desk takes
  the pixels, the desk's frame is what is lit, a cue lands mid-stream, and after
  a release its own look is running again

And on this end:

- a `link` command on a DMX show is refused, by name, without killing it
- the viewer draws a relic row only when the config names one, opens with
  `pixels` lit, and never lights `release` — it is an action, not a mode
- `config/obelisk_usb.json` and `config/obelisk.json` resolve to the same 344
  fixtures at the same channels with the same coordinates, which is the check
  that stops two files describing one sculpture from drifting

Not covered, and not coverable here: the USB stack, the WS2812 write, and
timing. `--probe-relics` finding nothing on a machine whose only serial device
is the DMX widget is the one on-hardware check that has run.

`readme.md` has a first-light order to work through with the sculpture in front
of you, arranged so each step proves one thing and the cheap ones come first.

And, for the obelisk as a device:

- the config resolves to 344 pixels at consecutive channels, highest 1032, with
  no overlaps and no warnings — and **python and the executable name and place
  every one of them identically**, which is now a test rather than a claim
- reaching past 512 is silent on a preview rig and an error the moment the same
  patch is pointed at `enttec_open`
- the coordinates are the sculpture's own, strip by strip, down-strips reversed
- the four seasons land on the four sides, and a strip is not flat — the two
  things that fail silently if the coordinate space is wrong
- the 173 pixels past channel 512 reach the frame stream
- `position_step` ramps a bank; a bank with a position and no step still sits
  where it always did, and one with neither still spreads evenly
- literal positions survive a round trip through python
- the viewer draws all 344 inside the canvas at three window sizes, drops the
  glow, labels the eight runs rather than the pixels, and does not repaint a
  frame already on screen

And, for the per-look knobs:

- every mythos26 look announces its own set, and a cue change replaces it
- a knob reaches the render: `floor` lifts the trough between beats, and
  turning `monochrome` off puts colour on the rig within a frame
- out of range is clamped both ways, and the clamped value is what the client
  sees the moment its command returns — the echo is emitted before the OK
- setting a knob twice does not duplicate it in the client's set, with frame
  lines interleaved between the command and the echo
- a name that is not a knob is rejected without killing the show
- the viewer builds a slider and a box per float and a checkbox per bool,
  rebuilds them on a cue change, takes a value the slider cannot land on, and
  puts the real value back when the box is given nonsense

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
python -m unittest discover -s python/tests -v      # 145 tests
```

It needs nothing installed. Anything requiring the executable or a display
skips itself when there is not one.

**Confirmed against a real Mixxx**, over loopMIDI, on the rig. The note map was
derived from the mapping's source rather than off a cable, and it was right.

What that first run did surface was the loudness backdrop reading the wrong
signal — see above. Which is the argument for `midi-watch` and `--midi-selftest`
existing: the map was right, the *choice of message* was not, and only playing a
track showed it.

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
- **Config hot reload** — restart to change the patch. Look, brightness and
  tempo are live over the control protocol.
- **Three of mythos26's seven states** — placeholders. That is the point of
  them, but they are not looks.
- **MIDI out, and MIDI for anything but tempo and loudness** — no
  control-change mapping to patterns, no faders. `MidiInput::handleMessage` is
  where that starts.
- **Bars** — the clock counts beats, not bars, because nothing upstream reliably
  says where a bar begins. `beat div 4` fires once every four beats but has no
  idea which of them is the one; a tap is what puts it there.
- **Sub-beat division** — `beat div` goes 1, 2, 4: slower than the beat, not
  faster. Eighths would need the envelope to shorten with them, which is a
  different pattern rather than a different number.
- **Stereo VU** — only the mono signals are read. The mapping sends left and
  right separately, which a rig split into two halves could use.
- **The viewer draws discs, not beams.** Fixtures with a real position in space
  are drawn as a flat 2D scatter; there is no notion of where a light is
  pointing, so it cannot show you a stage wash. It answers "is each fixture
  doing the right thing", not "what will the room look like".
- **The viewer's layout is 2D only.** `position` takes x and y; a rig hung at
  different heights and depths flattens.
- **The link has never met a sculpture.** Everything above it is tested,
  including a real `ObeliskCore` on a loopback, but no frame has reached a Pico.
  The USB stack, the WS2812 write and every question of timing are open.
- **The sketch is not compiled by anything here.** No Arduino toolchain on this
  machine; `eclipse-os.ino` is the one file on this branch that nothing builds.
- **One relic per config, and one port.** Nothing fans a look out to several
  sculptures at once.
- **No flow control on the link.** The desk sends at `device.fps` and the relic
  reads up to 4096 bytes a tick; if the desk is faster the excess is read and
  dropped a tick later rather than backing up, but nothing tells the desk to
  slow down.
- **The obelisk is drawn flat.** Its eight strips read as eight columns rather
  than as four sides of a pillar, because the viewer has no notion of a rig
  wrapping around anything. It answers "is each pixel doing the right thing".

---

## Open questions for you

1. **What are the last three looks?** `slot_5` to `slot_7`. Each is a
   `GeneratorHSV` in `mythos26.cpp` and a changed line in the table — say what
   they should do and they can be written.
2. **Do the beat looks want to stay flat?** Every fixture hits together. The
   coordinate space is there for a hit that travels along the rig, alternates
   odds and evens, or lands on a different colour each bar.
3. **Is 200ms right on the actual pars?** The envelope is three fields at the
   top of `Pattern_Mythos_BeatPulse`. The fixtures have their own response time
   and that is not measurable from a dry run.
4. **Is `Average` steady enough, or too steady?** The backdrop reads a
   two-second average with a further 0.25s of smoothing. If it feels sluggish,
   drop `baseSmoothing`; if it still moves too much, `Meter` is quantised and
   `baseGain` pulls the whole thing down.
5. **Do the other two loudness signals want a look of their own?** `Instant` and
   `Meter` are read and sitting there unused. Something that hits on transients
   is a natural fit for `Instant`.
6. **Is one universe enough long-term?** Ten 7-channel fixtures is 70 channels,
   so there is a lot of headroom, but multi-universe is a real change if it is
   ever needed.
7. **What happens on the first real frame?** The link is built and tested
   against a loopback, and has never met a Pico. `readme.md` has a first-light
   order; step 6 — pulling the cable mid-run and watching the obelisk take its
   own pixels back — is the one worth doing before trusting it in a room.
8. **How many relics at once?** One port per sculpture is fine for one. Three
   obelisks on one laptop is a different shape of problem — either three
   processes, or something that fans a look out, and that is worth deciding
   before rather than after.
9. **Is the obelisk's picture worth unfolding?** Its eight strips draw as eight
   columns. Drawing it as four sides of a pillar, or unwrapped with the sides
   marked, would make a look easier to judge — but it is the first thing in the
   viewer that would be specific to one rig's shape rather than read from its
   config.
10. **Is half a second the right holdover?** Long enough to ride out a missed
   frame, short enough that a dead cable is a stumble. Untested against a real
   one, and the number is one line: `RelicLink::setHoldover`.
11. **Should the desk match the relic's brightness, or the other way round?**
   The sculpture runs at `EBrightness::HIGH`, which is 63/255, and the link does
   not bypass it because it is a current limit. So the viewer is four times
   brighter than the thing it is a picture of. Either the desk dims its picture
   to match, or the relic's limit becomes a number the desk can read and
   compensate for — the second is more useful and more dangerous.
