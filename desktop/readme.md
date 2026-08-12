# eclipse-dmx

Runs eclipse-os patterns on a real DMX rig, from a desktop, through an Enttec
USB widget. Windows and Linux.

This is a replacement for QLC+ on a fixed install, not a layer on top of one.
There is no show-control stack in the middle: a config file describes the
patch, the executable renders frames and puts them on the wire, and a python
wrapper handles configuration and drives the running process.

```
config.json ──> eclipse-dmx ──> Enttec USB widget ──> DMX fixtures
                  ^   ^
   MIDI tempo in ─┘   │  line protocol on stdin
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

Under the picture is a split: the cue list on the left, and on the right the
running look's own knobs — see [tuning a look while it runs](#tuning-a-look-while-it-runs).
The two are separated because they answer different questions. The left is a
fixed table you learn the shape of; the right changes every time the left is
clicked, and mixing them in one column moved the buttons whenever a look with
more properties came up.

Three things about it are deliberate.

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
| `position_step` | with `count`, how far the position moves per fixture |
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
| `jacket` | the jacket's twelve looks, as a state machine |
| `mythos26` | the show — beat-driven, see below |

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

### the obelisk — a relic as a device

Everything above runs a relic's *look* on somebody else's rig. `config/obelisk.json`
is the other direction: the sculpture itself, patched as a device you can make
patterns for.

```sh
python -m eclipse_dmx view config/obelisk.json --pattern obelisk_seasons
```

344 pixels — four sides, two vertical strips each, 43 tall. Nothing reaches
hardware from this config; `config/obelisk_usb.json` is the same sculpture on
its cable — see [over usb](#over-usb).

Two things had to change to describe something that is not a truss of pars.

**The frame buffer is the rig's, not DMX's.** 344 pixels at three channels each
is 1032, twice a universe. That is only legal because nothing is putting it on a
DMX wire, so the 512-slot limit moved off the buffer and onto the outputs that
actually speak DMX. Patch the same file to `enttec_open` and the overflow is an
error again, correctly.

**Positions are coordinates, not an ordering.** A truss is one-dimensional and a
single 0..1 position along it says everything. The obelisk is not, and
`obelisk_seasons` reads x as *which side* and y as *how far up* — that is where
its four palettes come from. Collapse that to one number and every side gets the
same colour, which looks entirely plausible and is the wrong picture. So:

```json
"coord_space": "literal"
```

says the `position` fields are already in the pattern's own space and reach it
untouched. The default, `"normalized"`, is the existing behaviour: spread the
rig over 0..1 and let `coord_span_*` stretch it.

Same shape of decision as `addressing` — it changes what the numbers in the
*file* mean and nothing else.

The geometry itself is eight entries, one per strip, because `position_step`
lets one entry describe a run rather than a point:

```json
{ "profile": "rgb3", "name": "a_up", "address": 1, "count": 43,
  "position": [0, 0], "position_step": [0, 1] }
```

Those eight lines are the eight `GenerateAxisRow` calls in
`src/relics/obelisk/obelisk.cpp`, in the same order with the same numbers. Read
them side by side — if the sculpture is ever rewired, they both change.

The viewer switches to bare pixels above 64 nodes: one dot each, no glow, and
labels on the run rather than on every pixel. A glow is nine canvas items and tk
recolours them one at a time from python, so 344 of them would be 3096 calls per
repaint and the window would crawl.

### mythos26 — the show

`config/mythos26.json` is the show, on the same ten pars.

Unlike `jacket` and the `obelisk_*` looks, these are not a relic's patterns
borrowed for a rig — they are written for the rig, in `src/mythos26.cpp`. Two
things follow from that: the coordinate space is the rig itself (`coord.y` runs
0..1 from the first fixture to the last, with none of the stretching a relic
look needs), and they can read the beat.

Seven states, in the order the buttons show them:

| state | what it does |
| --- | --- |
| `beat_pulse` | the whole rig swells white on each beat |
| `vu_pulse` | the same flash on every *second* beat, over a red layer that follows the VU meter |
| `tv_static_mono` | every fixture a new grey, every frame |
| `tv_static` | every fixture a new colour, every frame |
| `slot_5` … `slot_7` | placeholders — a dim tinted breath, waiting for a look |

The placeholders are there so the cue buttons, the cross-fades and the config
all work before the looks exist. Writing one for real is a `GeneratorHSV` in
`mythos26.h`/`.cpp` and a changed line in `makeMythos26StateMachine()`; nothing
in the runner, the protocol or the viewer needs to know. If you rename a slot,
rename it in three places — there, `MYTHOS26_STATES` in
`python/eclipse_dmx/config.py`, and the button table in
`python/eclipse_dmx/viewer.py`.

##### how often it fires

`beat div 1 | 2 | 4` on stdin, or the **on 1 / on 2 / on 4** buttons in the
viewer: every beat, every other, or once a bar. It overrides every beat-driven
look at once, because a choice made at the desk should survive a cue change.
`beat div 0` — **auto** in the viewer — hands each look back its own default,
which is 1 for `beat_pulse` and 2 for `vu_pulse`.

What it divides is the beat *count*, not the tempo. Dividing the tempo would
stretch the envelope with it and the hit would go soft at slower divisions; the
point of "on twos" is the same crack, half as often.

Which beat of the pair or the bar it lands on is whichever one was current when
the count started — the clock counts beats, not bars, because nothing upstream
reliably says where a bar begins. A tap (`beat`, or `t` in the viewer) re-seats
it, which is how you move it onto the one.

##### `vu_pulse`

Two layers doing different jobs. Underneath, a red wash that follows Mixxx's VU
meter, so the rig has a floor that breathes with the music instead of going
black between hits. On top, the same white envelope as `beat_pulse`, on every
second beat — with a lit wash underneath, hitting every beat is too much light
and the hits stop reading as hits.

They composite by *desaturating*, not adding: at full flash the red has become
white, which is what a white flash over red looks like. Adding white to red
would give you pink.

**The wash follows the loudness of the track, not the waveform.** That
distinction is the whole difficulty, and getting it wrong twice is what these
two paragraphs are here to save you from.

*Which meter.* Mixxx sends several and they behave differently, so all three
useful ones are read and kept apart — a look picks one by name, never by note
number:

| `VuSource` | note | what it is |
| --- | --- | --- |
| `Instant` | 64 | the level right now, resent every 40ms — peaks on every kick |
| `Average` | 68 | the same averaged over ~2 seconds — the loudness of the *track* |
| `Meter` | 69 | a meter bar, quantised — moves in visible steps |

`vu_pulse`'s backdrop uses `Average`, and that is the whole fix for a wash that
flashes: `Instant` peaks on every kick, so a backdrop driven from it pulses at
beat rate no matter what you do downstream. `Average` is also one of the few VU
options the mapping enables by default. Reach for `Instant` when you want
something to *hit* on transients, which is a different look, not a broken one.

*How it is smoothed.* `baseSmoothing` is a symmetric one-pole — equally slow up
and down. It is deliberately not VU ballistics (fast attack, limited release):
that is how a meter is *drawn*, and because it snaps upward it keeps every
transient it is supposed to be removing. Default is a light 0.25s, because the
signal it reads is already averaged upstream; raise it if you point `baseSource`
at `Instant`. Zero follows the meter exactly.

`baseGain` of 1 maps a full-scale reading to full brightness; lower it to buy
the flash headroom above the wash.

A reading is held flat for 0.35s and then fades out over the next 0.45s. The
hold is so a backdrop sampled every frame does not ripple with how long ago the
last message landed; the fade is a dead-man's switch, because a level held up
forever after the link dropped would be the rig lying about having a signal.

This needs **Enable VU mono current** ticked in the mapping's settings — note
64, and the one VU option worth having on. Without it the red layer simply
stays dark and the flash still works.

##### the static looks

Every fixture a new random value every frame, in greys or in hue. No smoothing
and no motion, deliberately: the moment consecutive frames relate to each other
it stops reading as static and starts reading as a bad pattern.

The values come from hashing (frame number, fixture index) rather than from a
generator with state, which keeps `render()` const and stateless and makes a
given frame reproducible — the difference between a testable look and one you
can only eyeball.

**`beat_pulse`** swells: a rise of a couple of hundred milliseconds into a fall
of most of a second, in white. It was the other way round to begin with — 12ms
up and 200ms down, a crack with a clear dark gap before the next beat — and
that pair of numbers is the whole difference if you want it to hit again.

Its envelope is an **automation curve**, `eanim::AutomationCurveTrigger`, and
the beat is the impulse that fires it. `setEnvelope(attack, decay)` is the
shorthand for a rise and a fall; for any other shape, put keys on
`envelope.curve` directly:

```cpp
envelope.curve.clear();
envelope.curve.addKey(0.00f, 0.0f);
envelope.curve.addKey(0.20f, 1.0f, easing_functions::EaseOutCubic);
envelope.curve.addKey(0.80f, 0.0f);
```

The curve lives in `src/lib/eanim/`, not in the desktop tree, so the relics get
it too — it is a `FloatAttribute`, which means anything that already takes one
of those takes a trigger.

#### what a beat landing mid-pass does

The envelope is longer than a beat at any tempo this runs at, so at `beat div 1`
the fall never finishes. `envelope.retriggerMode` decides what happens then:

| mode | what it does |
| --- | --- |
| `Restart` | the timeline resets and the output steps to the curve's first value — a visible drop to dark on every beat, unless the curve fits in the gap |
| `RestartHold` | the timeline resets, but the output is **held** where it had got to until the new rise climbs past it. The default here: the beat still lands on time and still peaks, and the rig swells between a trough and full instead of cutting to black |
| `Overlap` | each impulse gets its own voice and the live ones are combined, `Max` or `Sum`. The old pass finishes underneath the new one rather than being cancelled by it. Four voices, then the oldest is stolen |

One detail worth knowing before you shorten the rise. The trigger reads the
curve's *peak across the frame* rather than its value at the end of one. With a
rise shorter than a frame — 12ms against 25ms at 40fps — sampling instantaneously
would reach full brightness only when a frame happened to land on the crest, and
every other beat would come out dimmer by a different amount. A rig flickering
unevenly on a steady tempo is exactly the artefact that avoids.

### tuning a look while it runs

A look's numbers — an attack time, a gain, a flag — are worth turning at the
desk rather than recompiling for. So a look can hand out the ones that matter,
and everything downstream builds itself from that:

```cpp
void Pattern_Mythos_TvStatic::reflect(ecore::PropertyBag& bag)
{
    bag.add("monochrome", monochrome);
    bag.add("floor", floorLevel, 0.0f, 1.0f);
}
```

One line per knob, and that is the whole registration. `bag.add` takes the
member itself, not a copy and not a setter, so the look goes on reading its own
field exactly as it did. Where a value is derived from — an envelope keeps a
built curve, not the two numbers behind it — pass a callback:

```cpp
bag.add("attack", attackSeconds, 0.0f, 1.0f, [this] { setEnvelope(attackSeconds, decaySeconds); });
```

Floats and bools only. This is for knobs, and a knob is one or the other;
anything richer belongs in the config.

The viewer puts the running look's knobs in the right-hand pane — a slider and
a box for a float, a checkbox for a bool. The slider is for finding a value and
the box is for saying one, because a 0..3 slider a hundred pixels wide cannot
express 0.15.

**The set belongs to the look, not to the pattern.** On a state machine, a cue
change replaces it wholesale, and the executable re-announces it every time, so
the panel follows the show without being asked.

Over the protocol:

```
params                      what the running look offers
param <name> <value>        turn one; on/off work for a bool
params dump                 the current set as one line of JSON
```

Values outside a knob's range are clamped rather than refused — the range is
what a slider spans, and a number typed slightly past it is a request for the
end of the slider. What it landed on is echoed back on a `PARAM` line *before*
the `OK`, so a client that waits for the reply and then reads has the new value
and not the old one.

Tuning is **live only**. Nothing is written to a config file behind you:
`params dump` is how a session that found something is kept.

```python
show.set_param("decay", 0.9)
show.get_param("decay")        # Param(decay=0.9, 0.01..3)
show.params                    # the whole set, replaced on every cue change
print(show.dump_params())      # {"attack": 0.15, "decay": 0.9, ...}
```

`ecore::PropertyBag` is in `src/lib/ecore/`, and `reflect()` is a virtual on
`eanim::GeneratorHSV`, so this is available to the relics and not only here.

### the beat

Beat-driven looks read `edmx::BeatClock`, one per process. Something upstream
says "beat now" and how fast; patterns ask how far into the beat it is, every
frame.

It **predicts**: beats arrive twice a second and frames render forty times a
second, so the clock interpolates from the last beat at the current tempo. And
it **free-runs**: if the link drops mid-set the rig keeps pulsing at the last
known tempo rather than freezing. It drifts, which is a much better failure than
a dark stage, and it re-locks the moment a real beat lands. `midi.free_run:
false` turns that off.

None of this needs a device. With no `midi` block the rig keeps its own time at
`midi.bpm`, which is what you want at a bench:

```sh
eclipse-dmx --config config/mythos26.json --dry-run --bpm 128 --frames 80
```

#### from Mixxx

`config/mythos26_mixxx.json` is this, configured. Five steps.

**1. Make a virtual cable.** MIDI does not travel between two programs on one
machine without one. On Windows that is [loopMIDI][loopmidi] (free); create a
port and leave it running. macOS has one built in: Audio MIDI Setup → Window →
Show MIDI Studio → IAC Driver → tick *Device is online*.

[loopmidi]: https://www.tobias-erichsen.de/software/loopmidi.html

**2. Start the cable before Mixxx.** Mixxx enumerates MIDI devices once, at
startup, and will not see a port created afterwards. This is the single most
common reason the port is missing from its list.

**3. Load the mapping.** Mixxx → **Preferences → Controllers** → click the
loopMIDI port → set *Load Mapping* to **MIDI for light** → **Apply**. The port
must show as enabled; the mapping is output-only, so nothing will appear to
happen yet.

**4. Set what it sends.** In that same panel, the mapping has a **Settings**
tab. Leave *Enable Beat*, *Enable BPM* and *Enable VU mono average* on — notes
50, 52 and 68, which is the minimum this needs. Tick *Enable VU mono current*
(note 64) if you want the transient-reactive source too. Turn **off** *Enable
MTC Timecode*; it defaults on and nothing here reads it. Note the *Midi Channel*
setting, default 1.

**5. Run.**

```sh
eclipse-dmx --list-midi                     # confirm the cable is visible
eclipse-dmx --config config/mythos26_mixxx.json
```

##### what it sends

Notes, **not** beat clock, on the mapping's Midi Channel:

| note | dec | meaning |
| --- | --- | --- |
| 0x30 | 48 | deck change |
| 0x32 | 50 | **the beat**, velocity 100 |
| 0x34 | 52 | **the tempo**, velocity = bpm − 50 |
| 0x40 | 64 | VU mono **current** — instantaneous, every 40ms |
| 0x44 | 68 | VU mono **2-second average** — what `vu_pulse` reads |
| 0x45 | 69 | first VU **meter bar** |
| 0x46+ | 70+ | the rest of the meter bars |

Two things follow, and both are already the defaults. `beat_note` is 50 rather
than "any note" — with the VU meters on, taking any note-on as a beat would
strobe the rig rather than pulse it. And the tempo arrives *explicitly* on note
52, which beats any interval we could measure, so `bpm_note` decodes it.

Notes 64, 68 and 69 are the exception to "turn the VU meters off" — all three
are read, into the three `VuSource` slots above. *Enable VU mono average* is
already on in the mapping's defaults; tick *Enable VU mono current* as well if
you want `Instant` available.

`clock` stays on regardless. Mixxx sends no 0xF8, so it costs nothing, and
`ticks=0` in `midi status` is a useful confirmation that what is on the cable is
what you think it is.

##### verifying it, rather than hoping

```sh
python -m eclipse_dmx midi-watch config/mythos26_mixxx.json --seconds 15
```

Play a track. This listens, prints every message that arrives, and tells you
which of them we are reading as the beat:

```
notes seen (channel, note, count):
  ch1    note 50    x32  <- taken as the beat
  ch1    note 52    x32  <- taken as the tempo
  ch1    note 48    x2
MIDI-STATUS port="loopMIDI Port" ... beats=32 bpm=128.0 src=midi_note lock=yes
```

If the numbers differ from 50 and 52, put what you actually see into
`beat_note` / `bpm_note` / `beat_channel` and run it again. Nothing is driven
while it watches — it forces a dry run, so a rig cannot flash at you mid-check.

The same thing live, once running: `midi monitor on` on stdin prints every
message; `midi status` gives the counts.

##### keeping the DJ controller out of it

A controller on the same machine — the S2, a Mixtrack, anything — is also a
MIDI input, is often the *only* MIDI input, and sends notes from every pad, jog
and transport button. It is simultaneously what `"auto"` would reach for and the
last thing that should be allowed to move the beat.

Two defences, and the shipped configs use both:

```json
"port": "loopMIDI",
"ignore": ["Traktor", "Kontrol"]
```

Naming the port is the real fix — a MIDI input is a separate stream, so once we
are on the cable the controller's messages are not something we filter out, they
are something we never see. `ignore` is the backstop for when the cable is
missing at startup and `auto` would otherwise fall through to whatever is left.
With every input ignored, it refuses, says so, and free-runs:

```
WARN midi: midi.port is "auto" but every MIDI input is on midi.ignore
           (0:Traktor Kontrol S2 MK3 (ignored))
```

An explicitly named port always wins over `ignore` — naming it means you meant
it. `auto` also ignores hardware brand names in its own preference list, for the
same reason.

##### the full block

```json
"midi": {
  "enabled": true,
  "port": "loopMIDI",
  "ignore": ["Traktor", "Kontrol"],
  "clock": true,
  "notes": true,
  "beat_note": 50,
  "bpm_note": 52,
  "vu_instant_note": 64,
  "vu_average_note": 68,
  "vu_meter_note": 69,
  "beat_channel": 1,
  "bpm": 128,
  "free_run": true
}
```

Naming a `port` counts as enabling MIDI, so `"midi": {"port": "loopMIDI"}` on
its own works. `"auto"` prefers a port whose name looks like DJ software or a
virtual cable and otherwise takes the only port there is; it will not guess
between several unrecognised devices, because opening the wrong input looks
exactly like opening none.

#### from anything else

`midi.clock` follows standard MIDI beat clock: 0xF8 twenty-four times a quarter
note, with 0xFA/0xFB/0xFC and Song Position Pointer. It costs nothing when no
0xF8 ever arrives, which is why it stays on for Mixxx.

Tempo comes off a rolling window of the last 24 ticks — the tick 24 ago was one
beat ago by definition, so it is exact after a single beat rather than
converging over tens of them.

Beat clock gives tempo but not, on its own, *phase* — a 0xF8 stream joined
halfway through a bar has no marker saying which tick is the beat. Start,
Continue and Song Position Pointer resolve that. If a source sends none of them,
`midi align` or a tap puts the downbeat where you say it is. If a source sends
both notes and clock, notes win, because a note is an explicit downbeat.

Everything downstream of "which notes arrive" is checked without a device:

```sh
eclipse-dmx --midi-selftest
```

It replays a synthesised Mixxx stream, a bare clock stream, a source sending
both, and a raw byte stream with running status and an interleaved realtime
byte, in synthetic time, and checks the beats and tempo that come out. It is in
the test suite, and it is what caught the clock tempo being a whole bpm out for
the first thirty beats.

At the desk: `beat` taps a downbeat, `bpm <n>` sets the tempo outright, and the
viewer has both on buttons plus `[t]` for the tap. `status` reports what the
clock thinks:

```
bpm=128.0 src=midi_note lock=yes free_run=on beat=417
```

`src` is where the tempo came from and `lock` is whether beats are still
arriving from outside. `lock=no` with the rig still pulsing means it is
free-running — the link went quiet and it is keeping its own time.

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

state <name>              states                 input <a|b> <on|off>
params                    params dump            param <name> <value>

bpm <float>               beat                   beat div <0|1|2|4>
midi open <spec>          midi close             midi align
midi free-run <on|off>    midi monitor <on|off>  midi list / midi status

link pixels               link cue               link release
link cmd <text>           link hello
```

`state` and `input` need a state machine pattern; the rest work on anything.
The `link` commands need `device.type` to be a relic — see
[over usb](#over-usb). In cue mode `state <name>` goes to the relic rather than
to our own state machine, so a cue list drives either end without changing.
The tempo commands work whatever is running, because the beat clock is
process-wide — dial a tempo in on `solid`, switch to `mythos26`, and it is
already right.

With `--emit-frames` on, a `BEAT <n> <bpm> <src> <lock|free>` line goes out the
moment each beat lands, outside the frame rate limit. That is what the viewer's
tempo readout is fed from.

A relic on the other end of a link talks back, and whatever it says arrives as
`RELIC <line>` — its answer to a Hello, and a note each time a takeover starts
or lapses.

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

The tempo side is there too, including a callback per beat:

```python
from eclipse_dmx import ShowController, list_midi_ports

print(list_midi_ports())

with ShowController("config/mythos26.json", on_beat=print) as show:
    show.midi_open("loopMIDI")     # or leave it to the config
    show.set_state("beat_pulse")
    show.wait(seconds=60)

    show.set_bpm(128)              # no MIDI? drive it by hand
    show.tap_beat()                # ...and put the downbeat here
```

`show.bpm`, `show.beat`, `show.beat_source` and `show.beat_locked` track what
the executable reports. Like frames, they only move when frames are being
emitted — passing `on_beat` turns that on.

Validation in python is stricter than in the executable, on purpose. A channel
claimed twice is a warning at runtime — a half-repatched rig should still light
up — but an error when you are generating config from code, where it is
almost certainly a mistake.

There is a CLI for the common jobs:

```sh
python -m eclipse_dmx ports                    # what serial ports exist
python -m eclipse_dmx midi                     # what MIDI inputs exist
python -m eclipse_dmx midi-watch my_rig.json   # what one of them is sending
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

**preview** (`"type": "preview"`) renders and drops the frame. For a rig whose
destination is the viewer rather than hardware. Unlike `console` it prints
nothing, so the log of a show you are watching stays readable, and it does not
claim the rig is a DMX one.

**relic_usb** (`"type": "relic_usb"`, or `relic_usb_cue`) is not a widget at all
— it is an eclipse-os relic on its own USB cable. See [over usb](#over-usb).

## Over USB

A laptop can take a relic over for a show and hand it back afterwards.

```sh
# flash the relic first: eclipse-os.ino, RELIC_OBELISK, USE_RELIC_LINK 1
eclipse-dmx --probe-relics
eclipse-dmx --config config/obelisk_usb.json

# or watch it and drive it at the same time
python -m eclipse_dmx view config/obelisk_usb.json --live
```

### first light, in order

Nothing here has met a real sculpture yet, so do it in this order and stop at
the first step that surprises you.

1. **Flash it.** `eclipse-os.ino` with `RELIC RELIC_OBELISK` and
   `USE_RELIC_LINK 1`. Leave `DEPLOYMENT 0` for now — the logs are worth more
   than the frame rate on a first run.
2. **Check it still runs its own looks.** Unplugged from any desk, the obelisk
   should behave exactly as it did before. The link costs it a branch per tick
   until something talks to it. If this is wrong, nothing after it matters.
3. **`eclipse-dmx --probe-relics`.** Expect one line naming `obelisk`. Nothing
   is lit yet; this is a seven-byte Hello and its answer. If the port is there
   but silent, the link is not built into the firmware.
4. **A serial monitor, and type `states`.** Expect
   `EOSLINK states seasons theater mono`. This proves the cue path end to end
   without a single pixel moving.
5. **`eclipse-dmx --config config/obelisk_usb.json --frames 60`.** Two seconds
   of `obelisk_seasons`, from the desk. Watch for `RELIC EOSLINK take` in the
   log — that is the sculpture confirming the takeover.
6. **Pull the cable mid-run.** The obelisk should go back to its own look inside
   half a second. This is the one that matters for a show.
7. **Then the viewer:** `python -m eclipse_dmx view config/obelisk_usb.json --live`.

If step 5 lights the wrong pixels rather than none, the patch and the strip
disagree — compare the Hello's `strip=0:344` against `--show-patch`.

Two things travel, and they are not alternatives:

| mode | what goes down the wire | costs | buys |
| --- | --- | --- | --- |
| **cue** | `state theater` | a few bytes, occasionally | the relic renders its own looks; survives a bad cable |
| **pixel** | 1044 bytes, 30 times a second | ~31 KB/s, continuous | *any* desk look on the relic — mythos26, the beat, the knobs |

`link cue` and `link pixels` switch between them live. Cues are sent in either
mode, because arming the look a relic will fall back to is exactly what you want
set before a stream drops.

### the takeover, and how it ends

The relic does not wait to be told. If no pixel frame has arrived for half a
second it takes its own pixels back and carries on rendering. A pulled USB cable
should cost you a look, not a dark sculpture in front of a room.

`link release` ends it immediately instead, and so does closing the show — a set
that ends should end, not fade out on a timeout.

While the desk owns the pixels the relic skips its own rendering entirely rather
than computing a look and overwriting it. On a 344-pixel relic that look is a
Perlin field per pixel per frame, which is the expensive half of a tick.

### the wire

One format, `src/lib/elink/`, compiled into both ends — the desk builds frames
with it and the relic parses them with it, so there is no second definition to
drift. Same reason the patterns are shared rather than ported.

```
EC 15 | type | len lo | len hi | payload | crc lo | crc hi
```

A CRC because the failure it catches is silent: a corrupt pixel frame is not an
error anyone sees, it is one wrong-coloured frame that reads as a glitch in the
look rather than as a bad cable. And a resync scan, because a relic gets plugged
into a running desk and the first bytes it ever sees are the tail of something.

Pixel payloads carry a strip id, a start and a count, so a frame does not have to
be the whole strip and a relic with more than one is addressable.

### finding the relic

`"port": "auto"` sends a Hello to every port and takes whichever answers by name.

It does not guess from the port name, because that cannot work: a Pico is USB
CDC and a DMX widget is an FTDI part, and on Windows both are `COMn` with a
registry description that names neither — this machine's widget reports itself
as `\Device\VCP0`. Guessing would mean a show quietly driving a widget that
ignores it.

`--probe-relics` is the same handshake as a diagnostic, and it is the first
thing to run when nothing lights.

### talking to it

A relic answers on its log stream, as text, and the show forwards those lines as
`RELIC ...`. A Hello gets back its name, its strips and their lengths, so a patch
that does not match the sculpture shows up at load-in rather than as a half-lit
rig during a set.

Typing into a serial monitor still works. `RelicLink` gathers printable bytes
seen between frames into a line and hands it to `handleCommand`, so `switch` or
`state theater` typed by hand does what it always did — the link replaced the old
`Serial.readString()` path rather than sitting beside it, because two readers on
one port eat each other's bytes.

### is the bandwidth there

Comfortably, and the number that looks like the limit is not one.

`Serial.begin()` reads like a baud ceiling and is not: on an RP2040 `Serial` is
USB CDC, where the rate is a value the host sets and neither end obeys. Real
throughput is USB's, on the order of 1 MB/s. 31 KB/s is about 3% of it.

The real ceiling is the sculpture. 344 WS2812s take ~30us each to clock out, so
`show()` alone is **10.3ms**, and the relic's loop sleeps 30ms a tick in a dev
build. Hence `"fps": 30` in the config rather than 40 — sending faster does not
draw faster, it just means frames are read and dropped a tick later. With
`DEPLOYMENT 1` the loop goes to ~20ms and 40 becomes reasonable.

### three things that would have bitten

**`DEPLOYMENT` used to disable serial input.** `USE_SERIAL_INPUT` was
`1 && !DEPLOYMENT`, so the build that goes on a sculpture for a show was exactly
the build with no way in. `USE_RELIC_LINK` is its own flag, on in both.

**Exactly one end applies gamma.** The sculpture's own path is HSV → `ColorHSV`
→ `gamma32` → pixel, inside `HSVStrip::updateStripPixel`. A streamed frame is
already RGB and goes through `HSVStrip::setPixelRGB` instead, straight to the
driver, so `master.gamma` on the desk is the only correction in the path and the
viewer and the sculpture agree. Plumbed through `strip_HSV` it would be gamma
twice, and everything below mid-brightness would crush toward black — quiet
failure, not a loud one, which is why it gets its own door rather than a flag on
the existing one.

**`Serial` may already be taken.** A relic built with `USE_SERIAL_MQTT` has a
line-based MQTT bridge on that port and binary frames would corrupt both. The
sketch refuses to compile with both on. The obelisk uses neither, so it is free.

### the sculpture is dimmer than the picture, and that is correct

`setGlobalBrightness(EBrightness::HIGH)` is 63/255, and the relic applies it on
top of whatever arrives. That is a current limit, not a preference — 344 pixels
at full white is about 20A — so the link does not bypass it. A Hello reports the
value so it is at least visible.

### verifying it without a sculpture

```sh
eclipse-dmx --link-selftest
```

41 checks over the frame format, the parser and the relic's end of the link:
resync after garbage, a corrupted byte caught by the CRC, an absurd length that
must not wedge the reader, a byte-at-a-time trickle, a frame sized for a longer
relic, the holdover expiring in synthetic time, and the obelisk's own 1044-byte
frame arriving intact.

The last block runs a **real `ObeliskCore`** — its geometry, its state machine,
`RelicCore::runTick` and the gating inside it — through a loopback. That is the
code the Pico executes, compiled with `USE_ARDUINO=0`, so the takeover, a cue
landing mid-stream, and the handback are all tested before anything is flashed.

What it cannot cover is what only exists on the device: the USB stack, the
WS2812 write, and every question about timing.

### what it does not solve

One relic per config and one port; nothing fans a look out to several
sculptures. And the relic's frame rate is its own — the desk cannot make an
obelisk draw faster than its `show()` allows.

## What is not here yet

- **Art-Net / sACN.** Only the USB widgets. The `DmxOutput` interface is the
  place to add one.
- **Multiple universes.** One 512-channel universe.
- **Moving heads.** RGB colour fixtures only; pan/tilt has no representation in
  the config yet, though a profile's `park` will hold them somewhere sensible.
- **White / amber / UV channels.** Profiles model dimmer + RGB + parked
  channels. An RGBW fixture works, but its white channel can only be parked at
  a fixed value, not driven from the colour.
- **Config hot reload.** Restart to change the patch. Look, brightness and
  tempo are live over the control protocol.
- **MIDI out.** Input only, and only tempo off it — no control-change mapping
  to patterns, no faders. `MidiInput::handleMessage` is where that would start.
- **Bars.** The clock counts beats, not bars, because nothing upstream reliably
  says where a bar begins. `beat div 4` fires once every four beats but has no
  idea which of them is the one; a tap is what puts it there.
- **Sub-beat division.** `beat div` goes 1, 2, 4 — slower than the beat, not
  faster. Eighths and sixteenths would need the envelope to shorten with them,
  which is a different pattern rather than a different number.
- **Stereo VU.** Only the mono meters are read. The mapping sends left and
  right separately, which a rig split into two halves could use.

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
