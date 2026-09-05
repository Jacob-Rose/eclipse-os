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

**The link runs on a real obelisk**: flashed, driven, cued and handed back — see
[it works on the sculpture](#it-works-on-the-sculpture). Getting the sketch to
build at all required removing `using namespace std;` from three headers, which
the arduino-pico core's move to C++23 had turned from untidy into fatal.

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

# the link, checked with no sculpture attached
.\build\eclipse-dmx.exe --link-selftest

# and with one plugged in and flashed
.\build\eclipse-dmx.exe --probe-relics
.\build\eclipse-dmx.exe --config config\obelisk_usb.json --frames 90
python -m eclipse_dmx view config\obelisk_usb.json --live

# reflash it without reaching for the BOOTSEL button
.\build\eclipse-dmx.exe --reboot-bootsel
```

Building the firmware is a separate toolchain — arduino-cli in WSL, plus
`tools/patch-libraries.sh` once. See the root readme.

On Linux the same thing with `./tools/setup-toolchain.sh` and `./build.sh`.

---

## Your rig

`config/uking_par36_x10.json` is the ten U'King Par 36 in 7-channel mode, and
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

### the UV par, on the same cable

`devices/uking_par36_x10.json` — the device the *show* uses — adds a U'King
ZQ01087 36W UV par at **71**, so that wire runs 1–77 and 435 channels are free.
Every UV unit in the room sits on that one address: they cannot be told apart on
DMX and are not trying to be.

71 and not 22. 22 is par_4's address, and a UV par sharing it would take par_4's
dimmer as its own. Address matters more than usual here because a device in this
codebase is one wire is one buffer — the UV is a fixture *inside* the truss
device rather than a device of its own, because two devices would mean two opens
of the same COM port.

Its chart, off QLC+'s own definition (`Fixtures/UKing/UKing-36W-UV-PAR.qxf`), is
the par 36's chart with the colours swapped for UV banks:

```
1 dimmer   2 UV bank 1   3 UV bank 2   4 UV bank 3   5 strobe   6 mode   7 speed
```

Which is why it needed no code: the patch layer already describes that shape.
The renderer writes r, g and b into the three banks, the fixture adds them, and
the UV comes up with how bright and how *pale* the look is — a saturated colour
drives it at about a third. It samples the middle of the truss (`position`
`[0.5, 0]`) rather than the end, which is where an eleventh fixture with no
position of its own would otherwise land.

`config/uking_par36_x10.json`, the bench rig, still patches only the pars. On
the same physical cable that means the UV holds whatever it was last sent, so it
can sit on through a bench session — patch it there too if that bites.

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

### when a hit lands is one decision, not one per look

The UV flashed out of step with the truss, and the reason was not the envelope —
it was that every beat look worked out its own hits. Each one read the clock,
watched the beat number, checked its rate and fired, which is four answers to a
question that has one. They came apart three ways, and all three read on a rig
as the UV not being with the show:

- **A look that is not showing is not counting.** Only a machine's active state
  ticks, and the incoming one mid-fade. A look entered cold had stale
  bookkeeping.
- **Entry always fired a hit.** Deliberately — a cue that comes up dark reads as
  a cue that did not come up — but it fired at whatever fraction of a beat the
  operator pressed the button, which is off the grid by construction. `layer uv
  state flash` was exactly that gesture, so the UV's first flash was always
  wrong and everything after it was counted from there.
- **A cross-fade ticks both looks**, so two envelopes ran for the length of it.

So the decision moved out of the looks and into `edmx::TriggerRack`
(`include/edmx/beat_trigger.h`), which is ticked **once per frame, before
anything else ticks** — that ordering is the whole guarantee. Two looks on the
same rate now read the same answer instead of each computing one, so they fire
on the same frame with the same sub-frame offset.

**The rate knob is the binding.** There is no string property on a property bag
and there does not need to be one: the four musical rates are the four triggers
(`bar`, `half`, `beat`, `double`), and a look's existing `rate` is which of them
it is on. Half time is now one decision for the rig rather than one per look
that happened to agree.

**What a look still owns is the shape.** A trigger says *when*; the curve on the
look says what the light does about it. That split is the point — the UV cracks
in 20ms and the truss swells over 150ms, on the same beat — and it is what makes
the curve editor useful on a layer: drawing the UV's envelope changes its shape
without touching its timing.

**`entry_hit` is the old compromise, made a knob.** On for the show's cues, which
must not open dark; off for the UV, which is joining a grid the rig is already
running. And when a cue *does* ask for a hit on entry it asks the shared trigger,
so everything on that rate comes up with it — a cue change is a rig-wide event
and now looks like one.

`SharedBeatTriggers` in the test suite covers the part that was actually broken:
entering `flash` at 0.13, 0.27 and 0.41 of a beat, and checking the first flash
lands on the grid rather than under the operator's thumb.

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
  devices/         one file per physical thing: the obelisk, the ten pars
  config/          environments - rooms with devices in them, and the show
  scenes/          Synesthesia scenes that take their colour from the rig
  python/          the wrapper package, the viewer, and the OSC sender
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
| `edmx::MidiInput` | tempo in, off winmm, the ALSA sequencer, or a rawmidi device |
| `edmx::Device` | one physical thing: geometry, addressing, its own output |
| `edmx::Config` | an environment: the devices in a room, and the show on them |
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

**The show's own list is empty again**: `slot_1`–`slot_12`, twelve placeholders
waiting for a look, because the show is being written from scratch. The looks
that were on it are still here, on the lists they belong to:

| look | where it is |
| --- | --- |
| `beat_pulse` | a cue on the `audio` machine — the whole rig swells white on the beat, on an automation curve |
| `tv_static_mono`, `tv_static` | cues on `generic` — every fixture a new grey, or a new colour, every frame |
| `vu_pulse` | removed; the flash over a loudness backdrop is in the history below and in git |

A beat look opens on the beat, and its envelope is stated where the cue is made
rather than inside the look — `beat_pulse` opens at 0.15/0.60. Attack and decay
are what a beat look *is*, so they belong there rather than in a constructor;
they stay live knobs at the desk once it is running.

**How often a look hits is a `rate` knob** beside them, in hits per beat: 0.25,
0.5, 1 or 2, snapped, because a slider will otherwise hand over 1.37 and 1.37
hits a beat is a rig drifting against the track. It divides the beat *count*,
never the tempo — the point of half time is the same hit half as often, and
stretching the envelope with the rate would soften it instead. So double time
keeps the 750ms envelope it had inside a 250ms gap and the rig hovers rather
than pulses; that is correct, and `decay` is the next knob along.

**Where the slow rates land is the clock's bar.** Four beats from the last
downbeat it was told about — a MIDI Start, or `midi align` — so half time takes
the one and the three of that bar, and quarter time takes the one. The one is an
assumption until someone declares it, because Mixxx sends beats and says nothing
about bars, and `midi align` on the one is the single gesture that fixes it for
every look at once. A `beat` tap deliberately is not that gesture: tapping a
tempo in means tapping every beat.

**There used to be a divider** — `beat div 1|2|4`, and on 1 / on 2 / on 4 in the
viewer. It is not coming back: the rate is per look, which the divider was not,
and one look in half time is a decision where every look in half time at once
was a mode. But the reason `4` was dropped from it — a clock counting beats with
no idea which of them was the one — is what the bar fixes, so once a bar is back
as `rate 0.25`. In between there was a per-look version of the bar, seating half
time's pairs on the beat you set the rate on; it could not be shared between
looks, had to be re-made on every cue change, and was counted off beat messages,
so it wandered between gestures. `beat div` is still rejected outright rather
than ignored, because without that the word falls through to the tap and an old
cue file would shove the grid instead — the error now points at `param rate`.

**And the count those rates are counted off is now a count of beats, not of
messages.** A real Mixxx cable sends the same beat twice, sends nothing for the
next one, and puts the other deck's beats over this one's; every one of those
moved a half-time look onto the other half of the pair, where it stayed until
the next one moved it back. `BeatClock::markBeat` matches each message against
the grid instead — too soon to be a different beat and it is dropped, two beats
along and the count moves by two, nowhere near the grid and it is ignored unless
a second agrees with it. `--midi-selftest` replays a stream that does all three
and checks the bar never moves.

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

`vu_pulse` was a white flash over a red wash that tracked how loud the track is.
The look has gone with the rest of the show's cue list, but the wiring under it
has not, and both wrong answers were plausible enough to be worth recording -
the next thing that reads the meter will meet them again.

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
| `Average` | 68 | ~2s average — the loudness of the track |
| `Meter` | 69 | a meter bar, quantised |

**Both colours are knobs** — `base_color` for the wash, `color` for the flash,
which is the name `beat_pulse` gives its own because the flash *is* a
`beat_pulse`. Each colour's own value is a ceiling on its layer, so a dark pick
is a dark layer rather than a no-op.

The composite is the interesting part, and it took three goes. Adding the flash
to the wash gives pink for white over red, so that went early and the look
desaturated instead: pull the saturation out of the wash and a white flash
arrives at exactly white. Making the flash colour pickable broke that, because
"desaturate toward the flash" had to become an interpolation, and interpolating
the *hue* walks the rim of the wheel — blue over red came out **green** on the
way, which is what it looked like on the rig.

What it does now is blend the two layers as **chroma vectors**: hue as an angle,
saturation as a radius, interpolated straight through the middle of the wheel.
Hues far apart lose saturation between them and pass near white — red to blue
goes red, pale magenta, blue — which is what a flash washing a colour out looks
like. And white has no chroma at all, so the vector shrinks to the origin, the
hue never moves, and the original desaturation falls out of the same arithmetic
rather than being special-cased. Mixed once a frame in `tick()`, because the rig
is one colour and `render()` runs per fixture.

**Mixxx sends notes, not beat clock**, which is the thing worth knowing before
touching any of this. Its MIDI-for-light mapping puts the beat on note 50, the
tempo on note 52 as `velocity + 50`, and VU meters on notes 64 and up, many per
second. So "any note-on is a beat" — the obvious default — would strobe the rig
rather than pulse it, and the tempo is better read off note 52 than measured off
intervals. Both are the defaults; see the table in `include/edmx/midi_input.h`.

This part is now **confirmed against a real Mixxx** over loopMIDI, which it was
not when the note map was first written.

### the DMX widget on Linux goes through libftdi

Found by driving a real truss, and the reason the Linux DMX path had never
actually lit anything: **no Linux API says when a frame has left the FTDI.**

`tcdrain()` returns when the kernel has handed the bytes to USB, not when the
chip has clocked them onto the wire, and 513 bytes at 250000 8N2 is 22.6ms of
transmission. The next break therefore lands *inside* the previous frame, which
a receiver reads as a packet starting mid-packet and throws away. Every frame is
wrong, permanently, and nothing reports an error — the port opens, the measured
line rate is 249400, the framing reads back 8N2, `TIOCSBRK` succeeds, frames go
out at 40fps and the widget's LED blinks. The rig is dark apart from the rare
frame that happens to align.

So `EnttecOpenOutput` now computes the wait from the frame's own length
(`bytes x 11 / 250000`) and spins it out before the break, and talks to the chip
through **libftdi** rather than the tty — a guard delay alone was not enough on
the serial path at any frame rate tried. libftdi is `dlopen`'d exactly as
libasound is, so nothing new is required to build. See
`include/edmx/ftdi_dmx.h`, and the measurement table in readme.md under
[the widget that would not light].

It also ends the permission problem: `/dev/ttyUSB0` is group `uucp` and udev
recreates it on every replug, whereas `69-libftdi.rules` tags the USB node
`uaccess` and the logged-in user gets an ACL for free.

Verified on the truss: ten U'King par 36 lit from `eclipse-dmx` itself, then the
whole `mythos26` show beat-driven at 128bpm.

### the same thing on Linux, without a cable to install

Mixxx on Linux is not on the other end of a device node, and that is the whole
of the problem. Linux has two MIDI worlds: **rawmidi** device nodes, which is
where a USB controller lives and what `MidiInput` used to read, and the **ALSA
sequencer**, a patchbay where applications publish ports. Mixxx owns no
hardware, so it has no device node — it is on the sequencer, and a rig that only
reads `/dev/snd/midiC*` sees every controller in the room and not the one
program playing the music. `--list-midi` on a Linux laptop with Mixxx running
printed `OK 0 midi inputs`.

The asymmetry that decides the design: Mixxx does not *publish* a port for
something else to read, it goes looking for one to **write into**. So the port
that has to exist is ours. `MidiInput::open` now publishes `eclipse-dmx IN` on
the sequencer, and Mixxx is pointed straight at it — no loopMIDI equivalent to
install, because on this platform the rig *is* the cable. It still subscribes to
a named source when `midi.port` names one, and still opens a device node when
given a path.

Three things fall out of it:

- **`auto` no longer has to refuse.** Its refusal exists because opening the
  wrong input looks exactly like opening none. On the sequencer there is a third
  option that cannot be wrong — publish and wait — so that is what it does, and
  `auto` is now the right value for `midi.port` on both platforms.
  `config/mythos26.json` says `auto` instead of `Mixxx` for exactly that reason.
- **A lone application is never taken as "the only port there is".** That rule
  is sound for device nodes and nonsense on a sequencer, where every running
  program is a port and "the only one" is an accident of what is open.
- **libasound is `dlopen`d, not linked.** Reaching the sequencer needs it;
  linking it would mean this refuses to build without the dev package and
  refuses to start without the runtime, on machines whose job is pushing DMX.
  Loaded by hand, a machine without it builds, runs, and falls back to device
  nodes. That is `src/alsa_seq.cpp`, and it costs one table of function
  pointers. Sequencer events are decoded back into MIDI bytes rather than read
  as structs, so the note handling downstream is the same code on every
  platform and `--midi-selftest` still covers the path Linux runs on.

Verified end to end on this machine: `eclipse-dmx IN` published and visible to
`aplaymidi -l`, a synthesised MIDI-for-light stream played into it, 16 beats
counted and 120bpm read off note 52. Both directions of `aconnect`, subscription
by name and by `client:port` address, and close/reopen without hanging the
reader thread.

`config/mythos26.json` is the show with all of that named
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

Floats, bools and colours. The first two are what a knob usually is; a colour is
the one thing that is not and still has to be turned at a desk, because the
alternative was three sliders spelling one out in hue, saturation and value and
nobody picks a colour that way. It registers the same one line — `bag.add(
"color", pulseColor)` — travels as `#rrggbb`, and gets a swatch in the viewer
that opens the system picker. Anything richer than the three is config.

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

### devices and environments

The config used to be a rig. It is two things now, because it was always two
things pretending to be one.

A **device** is a physical object — a sculpture, a truss of pars. It owns its
geometry, how its numbers are read, and its own way onto a wire. `devices/`
holds them, described once, referred to by name.

An **environment** is a room with things in it and the show running on them: it
lists devices, says where each sits in the pattern's coordinate space, and holds
the master, the pattern and the tempo. `config/mythos26.json` is the obelisk and
the ten pars together, on one look.

**One pattern renders once, across everything.** That is the whole point. The
show loop renders into one colour buffer spanning every device in order, and
each device takes its own slice into its own frame buffer and its own wire.
Devices do not even share a frame rate — the obelisk draws at 30 and the truss
at 40, so the loop runs at the faster and each sends when it is due.

A config with `fixtures` at the top level and no `devices` array is read as an
environment holding one device at the origin, which is exactly what every config
written before this is. All five existing ones load unchanged.

**The device's coordinates stay the device's.** This is the part that was got
wrong first and is worth recording. The obvious move is to normalise every
device into the pattern's 0..1 so they compose — and it destroys them: the
obelisk's 0..7 across its faces is the number `obelisk_seasons` reads to pick a
palette per side, so squashing it turns the sculpture one flat colour. So
placements are an offset by default, `fit` exists for when a rescale is genuinely
wanted, and `fit` takes `null` per axis so you can fit the one the pattern reads
and leave the other alone.

### the window is a desk now, not a canvas

Each device gets its own panel inside the main window — dragged by its title
bar, sized by a corner grip, laying its fixtures out into whatever room it ends
up with. Children of the window rather than real OS windows, so they move with
it and cannot be lost behind it. FL Studio's plugin windows, and for the same
reasons.

Drawing an environment into one shared canvas does not work at all: 344 pixels
and ten pars have nothing in common as a picture, and the sculpture squashes the
pars into a corner.

How much room each panel opens with is `view_scale` in the environment. Two
things about it are deliberate:

**It is a view property.** It touches nothing the pattern sees. The executable
parses it only so the schema has one definition, and reports it in
`--show-patch`; the viewer is what acts on it.

**It is explicit, not inferred.** The first attempt derived panel widths from
each device's own aspect ratio, which is clever and wrong: a pillar and a truss
plainly want different panel shapes, but *how* different is a judgement about
the room and the screen. That is a number you tune by looking at the window, not
one a fixture list can tell you.

### the obelisk's down strips were a pixel out

Found by looking at the sculpture in the new window.

`ObeliskIO::init` built its four down runs starting at `WALL_SIDE_LENGTH`, so an
up run covered y 0..42 while the down run beside it covered 43..1 — a whole unit
high, on strips whose pixels physically line up. Anything reading y, which is
every look on the sculpture because that is what the coordinate is *for*, saw
half the obelisk shifted against the other half. Subtle on a noise field and
obvious on a gradient.

They start at `WALL_SIDE_LENGTH - 1` now and every run covers y 0..42. Pixel
counts are untouched: 43 per run, 344 total. Fixed in `obelisk.cpp` and in the
three configs that mirror that geometry, which have to agree.

**Flashed to the sculpture**, so the fix is on the hardware and not only in the
picture.

### --reboot-bootsel could have rebooted a DMX widget

Also worth recording, because it was a hazard I built and then walked into.

With no relic answering, the 1200-baud fallback took "the only port" as its
target. On a machine whose only serial device is the Enttec widget — which is
this one, whenever the obelisk is unplugged — that aims a reboot-to-bootloader at
a DMX widget mid-show. It refuses now and asks for `--port`, because a relic
flashed before the link existed is exactly the thing we cannot tell apart from a
widget, and guessing is not worth being capable of.

### it works on the sculpture

Flashed and driven, on a real obelisk. What it said for itself:

```
RELIC COM4  obelisk strip=0:344 brightness=63
RELIC EOSLINK take
RELIC EOSLINK states seasons theater mono
RELIC EOSLINK state theater
RELIC EOSLINK release asked
RELIC EOSLINK take
```

That is, in order: it names itself and its one 344-pixel strip, it accepts a
takeover, it answers a cue query, it takes a cue, it hands the pixels back on
`link cue`, and it takes them again on `link pixels`. A show killed with no
release left it answering a Hello three seconds later, so a desk vanishing
mid-frame does not wedge it.

Firmware is 396 KB of flash (18%) and 71.5 KB of RAM (27%).

And it **looks right on the sculpture** — watched during the run, so the patch,
the strip order and the coordinate space all agree with the physical object.
That is the half no protocol reply can tell you: a rig can answer every frame
correctly and still be lighting the wrong pixels.

### two USB CDC facts that made a working relic look dead

Both cost real time, and neither is visible from anything the relic does. Worth
having written down because the symptom is identical to "the firmware is broken".

**DTR has to be asserted for CDC, and must not be for a DMX widget.** A CDC
device reads DTR as "a host is here" and discards everything it writes until it
is set. `SerialPort::open` deliberately left DTR alone — with a good comment
about FTDI boards that wire it to a reset or an RS485 driver-enable, where
asserting it holds the thing mute. Both are right; it is a parameter now.

The symptom was `--probe-relics` finding nothing while the relic was running
perfectly and answering the byte-identical frame sent by hand from PowerShell a
moment later. Which is what finally located it: the difference between the two
was DTR and a delay, and nothing else.

**A CDC port is not ready when `open()` returns.** The host has to bring the
line up and the device has to notice; anything written into that gap is gone.
An immediate write is lost, 300ms later is reliable. Both the probe and
`RelicUsbOutput::open` wait now — without it the first frames of every show
would vanish, which reads as a rig that takes a moment to warm up rather than as
a bug.

### the toolchain moved to C++23, and our headers could not follow

`eclipse-os.ino` had never been compiled on this machine. Getting it to build
turned up one real problem, and it was ours.

arduino-pico 6.0.0 builds at **`-std=gnu++23`**, and three headers here did
`using namespace std;` at global scope — `ecore/name.h`, `ecore/range.h` and
`eio/relic.h`. That opens `std` for every translation unit that includes them,
so `std::byte` collided with Arduino's `byte`, `std::lerp` with `ecore::lerp`,
and the errors landed deep inside `SPI.h` and `Common.h` where nothing looked
wrong.

The readme's two SPI patches existed to work around exactly this. They are gone
now: the headers name what they need instead. `std::map` is *not* among them —
Arduino has its own `map()`, and importing `std::map` beside it is the same
collision in a smaller box, so that one stays qualified.

The one remaining third-party patch (AnimatedGIF skipping `<Arduino.h>` because
the core defines `PICO_BUILD`) is now `tools/patch-libraries.sh` rather than
prose: idempotent, keeps a `.bak`, and reports the patches it found unnecessary
so we learn when upstream fixes one.

### reflashing without the BOOTSEL button

`--reboot-bootsel`, and `link bootsel` on a show that already has the port open.

Two mechanisms, tried in that order. An elink `Reboot` frame is deliberate and
confirmable — only a relic running our firmware answers a Hello, so if one does
we know exactly what we are rebooting. The 1200-baud touch is the fallback and
is what every Arduino tool uses, so it also works on a relic flashed before the
link existed, which is precisely the board you most want to reflash without
reaching behind a sculpture.

`Reboot` is its own frame type rather than a command string on purpose: it
should be impossible to arrive at by fat-fingering a cue, and the relic does not
come back from it.

Confirmed on the obelisk, both directions: `--reboot-bootsel` took COM4 away and
put `RPI-RP2` back on E:, dropping the `.uf2` on it brought the relic back, and
it answered a Hello and took frames again — without anyone touching the button.

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

### a device with no wire does not stop the show

An environment names every device in the room. A bench rarely has all of them
plugged in, and half of what this program is for is building a look before the
truss exists — so a device whose widget is missing no longer refuses to start.
It stays in the show and keeps rendering: it is in the frame stream, the viewer
draws it, the pattern spans it. It simply has nowhere to send.

Loudly, in three places, because a device silently missing from a show is how
you find out at the venue: the log says so, the panel's own title bar says so
for as long as the window is open, and `main.cpp` emits

```
OFFLINE <device>: <reason>
```

`OFFLINE` and not `ERR` on purpose. On this protocol `ERR` is how a *command* is
refused, and `ShowController.start` reads a pre-READY `ERR` as a show that will
not run — which this is not. A UI reading one as the other either waits forever
for a reply or gives up on a show that is running fine.

What is still fatal: a config naming a device *file* that does not exist. That
is a typo, not an absent rig, and carrying on would light a room missing the
thing the show is about.

The startup path had a related bug worth naming. A refusal to start is written
and exited on immediately, so `poll()` routinely wins the race against the
reader threads and the caller got `exited immediately with code 1` — the exit
code is 1 for everything — in place of the reason, which is the only part that
helps. `start()` now drains the readers before concluding anything.

### the rig's colour, on the screen behind it

```sh
cd python
python -m eclipse_dmx osc --test                       # is anything listening
python -m eclipse_dmx osc ../config/mythos26.json --device synesthesia
```

The second client of the frame stream. It reads one fixture's colour and sends
it as OSC to a colour control in a visualiser — Synesthesia first — so the
visuals behind the truss are tinted by the same render that is lighting it, out
of one look, with neither side knowing about the other.

**One fixture, not the rig's average.** The mean of a palette sweep across a
truss is grey every time, and a primary colour that is always grey is worse than
no feature. `devices/synesthesia.json` is a probe: one fixture, no wire, placed
by the environment, whose only destination is the `F` line on stdout.

**The endpoint can be a name**, and that is not a convenience. `--address
mac-mini.local:6000` is looked up again every 15s while the set runs, and the
socket re-pointed if the machine moved. Connected UDP resolves once, at
connect, which is what makes the send path cheap — and on a DHCP network it is
also what aims the rest of a set at an address nobody is at, with the sent
counter still climbing and no error anywhere on either end. A failed look-up
keeps the address in hand rather than tearing a working link down over a wifi
blip, and a name that has not resolved at startup no longer refuses to run the
show: the machine is asleep, the rig is not. IPv4 wins where a name answers
with both, because an OSC input bound to `0.0.0.0` cannot be reached over v6
and that failure is invisible from here too.

### and the audio engine, back the other way

The same wire, inbound: Synesthesia Pro sends its `syn_*` audio uniforms over
OSC, and `osc_input.py` binds them to this rig's knobs. `--osc-in 7000` on
`run`, `osc` and `view`; `config/oscmaps/synesthesia.json` beside the config;
`osc-watch` to see what is arriving.

The reason to want it is that **eclipse-dmx hears nothing**. Its beat is MIDI
notes from Mixxx — a grid and a VU meter. Synesthesia is doing a real FFT on
the same music and publishing bass/mid/midhigh/high levels, transients,
presence, a beat detector and a BPM estimate, forty-odd values a frame. None of
that is computable here and all of it is already on the network.

Three decisions in it are load-bearing:

**The bindings match on a glob, not an address.** Synesthesia documents the
uniforms by name and does not document the OSC addresses they arrive on, and
v1.20 renamed them. So `*bass*level*` is the binding and `osc-watch` is how you
find out what to write — the same shape as `midi-watch` for Mixxx's notes, for
the same reason. `exclude` exists because the useful patterns overlap: without
it, a binding meant for `syn_Level` silently also takes the four band levels
and one knob has four sources.

**The actions are the MIDI mappings' actions.** Same registry, same
`ActionContext`, same files-are-presets shape — a pad and a bass drum should be
able to do the same things. Three new ones (`param`, `master`, `bpm`) serve
both sides, and nothing in either dispatcher knows the other exists.

**Rate is part of the binding.** Sixty values a second per uniform, down the
pipe the cues use, is the failure mode. A `value` binding has a minimum
interval and a minimum change; a `trigger` has neither, because dropping a beat
is what that mode exists not to do. Streamed knobs go out with
`expect_reply=False` — a round trip per value would serialise the sender
against the show's loop.

**It is python and not C++** because the whole feature is "read a colour that is
already arriving, and put it in a UDP packet". `--emit-frames` already streams
it; `ShowController` already parses it. No build, no new transport in the show
path. The cost is one process hop of latency, on a colour wash, which nobody can
see. When it needs to outlive the wrapper it becomes an `OscOutput` in the
binary and the packet builder moves with it unchanged — OSC 1.0 is an address, a
type tag string, and big-endian floats, each block padded to four bytes, which
is thirty lines with no dependency.

**What it cannot tell you is whether any of it arrived**, and the command says
so rather than reporting "88 sent, 0 dropped" and stopping. UDP is
unacknowledged; a closed port is supposed to answer with ICMP unreachable, and
on Windows loopback that is simply not delivered — so a whole set aimed at a
visualiser that was never started reports zero errors. Hence `--test`, which
sweeps a hue with no show and no hardware: it separates "the app is not
listening" from "the show is not producing a colour", and those look identical
from the desk.

The gamma is undone before sending. `FixtureMap::render` bakes `master.gamma`
into every channel because an LED is linear in duty cycle and an eye is not; a
shader is downstream of a display that corrects again, and correcting twice
reads as washed out. Read off the config, so a show with gamma off does not get
it undone.

**The scenes are in `scenes/`**, and they are part of the feature rather than
decoration next to it — the control a scene exposes *is* the sender's target:

- `eclipse_link_test` — a test card. Deliberately not audio reactive: everything
  on it is the incoming colour or a clock, so anything moving is evidence about
  the link. A card that moves with the music cannot tell you whether it is also
  moving with your sender.
- `eclipse_chroma_key` — the look. The media in black and white except where it
  already matches the rig's colour, within a limit. Chroma-distance matching by
  default, so a colour in shadow still counts; hue-only for when a wash comes
  back off a wall paler than it left.

The addressing is the part that bites. `/controls/global/color/1` is
**positional** — the first colour control in the order a scene declares them —
so it points somewhere different in every scene and does not survive a scene
change. Every scene here names its key colour `rig_color` and declares it first,
so the stable form `--control /controls/scene/rigcolor` drives any of them.

### four things about Synesthesia that cost an evening

All four present as the same two symptoms — a black screen, or a scene that sits
at its defaults — and none of them reports anything, anywhere.

**A scene without `GPU` in its `scene.json` loads and renders black.** 33 of the
36 scenes on this machine declare it; the three that did not were ours. Every
scene the app ships, every marketplace scene and every scene the app's own
editor writes has it. `0` is the template's value and the right tier for all of
these. There is a test for it now, because nothing else catches it: the tile
appears in the browser, the shader is valid, the screen is black.

**OSC input ships switched off, and the port is 6000, not 8000.** Off means the
packets are discarded by the OS with no error at either end, which is
indistinguishable from a wrong port, a wrong address, or a scene with no colour
control. It is also a Pro feature. Both facts are readable without opening the
app — `OSC_INPUT` and `OSC_INPUT_PORT` in its `preferences.json` — and the
decisive check is whether anything holds UDP 6000 at all:

```powershell
Get-NetUDPEndpoint -LocalPort 6000
```

**There are two media slots and they answer to different calls.**
`_isMediaActive()` covers video and webcams; a still goes to a different slot
and needs `_exists(syn_UserImage)` / `_loadUserImage()`. Worse, the flags and the
samplers disagree: `eclipse_media_probe` samples both slots unconditionally and
showed footage that every flag denied existed. So `eclipse_chroma_key` asks all
three signals and offers a dropdown to overrule them. `PixelPopArt` gates on
`syn_MediaType >= 0.5` and has no `MEDIA` key at all, so that key is not what
enables media either.

**`syn_FadeInOut` is a legitimate black screen.** Multiplying the output by the
VJ's master fade is what the docs suggest, and it means a fader at zero or a
deck that is not on air blacks the scene with nothing wrong. It was the only
one of our four scenes to do it, so the symptom was "this one scene is black and
the rest are fine". It is a toggle now, off by default.

The scenes are deliberately a ladder — `min` has no passes and no textures,
`link_test` adds a feedback buffer and a second pass, `chroma_key` adds media —
so a scene that fails says which addition broke it. That is how three of the
four above were found.

---

### the audio bus, and the analysis as an ordinary parameter

The version above drove three knobs, and that was its ceiling. A binding aimed
straight at a knob has to know which look is running and what its knobs are
called — so it is written per show, it dies when the look changes, and every
new audio-reactive idea is new wiring. Meanwhile the engine already had a
three-slot `AudioLevel` singleton fed only by Mixxx's VU notes, which patterns
read by name (`vu_pulse`), and the two never met: Synesthesia's analysis could
not reach a pattern, and a pattern could not read anything but Mixxx.

So there is one bus now, twenty-one named channels, and two sources that fill
the same slots.

**`AudioChannel` replaces the three-slot `VuSource`.** The names are
Synesthesia's, lowercased — `bass`, `midhigh_hits`, `bass_presence`. `VuSource`
survives as three aliases (`channelFor`), so `midi_input.cpp` and `vu_pulse`
are untouched, and Mixxx's two-second average *is* `level`, the same slot
`syn_Level` fills. That aliasing is the whole reason `audio.source` can be a
switch rather than a rewrite.

Everything is 0..1, `bpm` included — scaled on the way in, so nothing
downstream special-cases one channel's units. `syn_*Time` and the BPMSin/Tri
waves are deliberately not on the bus: they are unbounded or generated, and the
hold-and-decay dead-man's switch means nothing for a value that only counts up.

**A modulation is the load-bearing idea.** `mod <param> <channel> [low] [high]
[slew]`, declared in a config or typed at the desk, and every frame the engine
writes `low + (high - low) × bus.get(channel)` into that property. Because
`ecore::Property` points straight at the pattern's member and fires its
`onChanged`, a knob driven this way is indistinguishable from one turned by
hand — so **no pattern needed changing to become audio-reactive**, and one
written next year will not either. That is the thing worth keeping: the feature
is a table in the runner, not a capability spread through the looks.

The bag is reflected fresh each frame rather than cached, for the reason
`PropertyBag` says not to keep one — on a state machine the object those
pointers point into changes when the look does.

**One knob has one driver.** Repointing replaces rather than stacks: two
modulations on one property is a race whose winner is whichever ran last, and
there is no reading of that a desk could show.

**A mod survives a cue change; a knob does not.** `mods` marks one whose
property the current look has never had as `no-param`. That is the only way the
failure is visible — a per-frame writer cannot complain sixty times a second,
which is also why `audio` is the one command that answers nothing.

#### additive layers, and the one thing that made them work

`"blend": "add"` on a layer sums with what the show put there. The guarantee
that makes it useful is that **a layer sitting at black is invisible** — so a
hit layer is safe to leave patched, rides over every cue rather than only the
one written for it, and a night with no visualiser costs the hits and nothing
else.

`ecore::HSV::add` would not have given that. It blends the two hues at a flat
50% however dark either is, so a black layer would drag the show's colour
halfway to red while emitting no light of its own. (It had no callers anywhere
in the tree, firmware included, so this was untested rather than relied upon; it
is left alone.) The compositor weights the hue by each side's share of the
light instead, which is what two lamps pointed at one surface actually do, and
degenerates correctly at both ends.

Layer `fixtures` also take a trailing `*` as a prefix match. The ring's 35
pixels are individually named and a `"count": 10` run becomes `par_1..par_10`,
so without it a layer over a *section* means pasting a list the device file
already has — and re-pasting it every time the rig changes.

#### slot 5, and three colours on three sections

`config/mythos26.json` ships three additive layers, one per section of the room,
in frequency order up it: `bass_hits` red on the scanner ring, `mid_hits` green
on the obelisk, `high_hits` blue on the truss. Each runs `solid` — one colour,
one level — with its level on a mod. Nothing in the pattern knows about audio;
it is a colour and a number, and the bus turns the number. A fourth needs no
new code.

`pars/par_*` and not `pars/*`, because the UV par is the eleventh fixture on
that cable and a blacklight strobing on every hi-hat is not what "blue on the
truss" means.

Slot 5 became `hits`: a near-black base so the three read as three colours over
nothing. Not literally zero — a rig that goes fully dark on a cue is
indistinguishable from a rig that has crashed, and the one place that matters
is the moment you press it in front of a room. The layers are not part of that
state and add over the other cues too; `hits` is the one that gets out of their
way.

#### mixxx or synesthesia, and what the switch deliberately does not do

`"audio": {"source": ...}` is one setting and it is **enforced, not advised**:
on `synesthesia` the runner does not wire the MIDI VU notes to the bus at all,
so the two cannot both write `level` and leave it flickering between two
readings of the same music. The notes still arrive and are still visible in
`midi monitor`, so nothing has to be turned off at the Mixxx end.

**It moves the beat too**, and that is the whole of what `source` means: one
source owns the music. `syn_BPM` sets the tempo, `syn_OnBeat` sets the phase,
and the MIDI cable is not wired to the beat clock at all in this mode. Set
`"bpm": false` for the one supported split — analysis from the visualiser, beat
from Mixxx's grid.

I built the split version first, with the tempo left on the MIDI cable. It was
the wrong instinct dressed up as caution. Having picked the app that is
actually listening to the music, taking its answer for how fast that music is
going is the consistent choice; the split is the thing that needs justifying.
And the split is also where the two quietly fight, which is the part worth
recording:

**`markBeat` learns a tempo from every gap between beats**, folding it into the
period at 25%. So any arrangement where one thing states a tempo and another
keeps delivering beats converges on the *beat spacing*, within a few beats,
whatever was stated. A rig taking `syn_BPM` while Mixxx's notes still reached
the clock would sit at neither tempo, and would look like it was working.

Cutting the cable does not fix that on its own, because **Synesthesia's own two
channels both imply a tempo** — `syn_BPM` states it, `syn_OnBeat` implies it by
when it fires. That is what `BeatClock::setTempoHeld` is for: while the stated
tempo is live, beats anchor the phase and stop teaching a rate. The stated
number is the better of the two, being smoothed inside the detector, where
`syn_OnBeat` comes over UDP through a rate-limited binding. This was found by
driving the two deliberately out of step — 130bpm stated, beats at 400 — and
watching the rig report 230.

Nothing happens unless the `bpm` channel is live. A channel nobody fills reads
zero, and zero would be 50bpm; instead the clock is left alone and free-runs,
which it is already built to do.

`midi status` grew `beat_from=` (the configured owner) beside the clock's own
`src=` (whoever last set it). They differ in exactly the case worth seeing at a
venue: Mixxx playing and the rig not following reads as `beat_from=osc` with
`src=internal`, which says the cable is not wired to the clock rather than that
the cable is dead.

The shipped map lost its direct `bpm`-action binding. With the tempo coming off
the `bpm` channel, that was a second competing route to one number, which is
the confusion `source` exists to remove.

Consequence worth stating plainly: flipping that one word back to `"mixxx"` is
a complete, working fallback. The beat returns to the cable, the eighteen
Synesthesia-only channels go dark, the hit layers sit at black and therefore
vanish, and every other cue runs exactly as it did.

Because a config can now declare the source, `--no-osc-in` had to become a real
override rather than merely the absence of a flag — otherwise the only way to
run a declared show without the visualiser would be to edit the config at a
venue.

---

### the bug that made the hit layers do nothing

The first time this ran against a real Synesthesia, nothing moved. The chain
was right at both ends and wrong in the middle, and the middle was a pairing of
two decisions that are each correct alone:

- a value binding **suppresses an unchanged value** — "a level holding still
  says nothing". Correct when a binding drove a *knob*, because a knob that has
  been set stays set. That is what these bindings did before the bus existed.
- the bus **fades any channel nobody has restated in 0.8s**. A dead-man's
  switch: without it a dropped link leaves the rig lit at whatever the music
  was doing when it went.

Put together with no keepalive, they delete the value. The channel is
suppressed at the python end, goes stale at the C++ end, and the look driven
from it fades to nothing while the music is still playing.

The fix is `Binding.max_interval`, default 0.2s — the longest a binding may
stay silent about a value that is not changing. Against the bus's 0.35s hold
that leaves room for one keepalive to be lost, which matters because nothing
retransmits UDP. A still channel now costs five lines a second instead of
thirty, rather than one and then none.

**Why the tests missed it.** The end-to-end test fed the bus with
`show.command("audio ...")` in a loop — which is not the path a set uses. It
proved the engine half and skipped the half that broke. `TheBindingKeepsTheBusAlive`
goes through the binding, and the old `test_a_level_that_holds_still_says_nothing`
became `..._says_little`, because it had been asserting exactly the behaviour
that was wrong.

Worth keeping as a shape: the failure was not in either component, it was in
the seam between one written for a stateless consumer and one written as a
stateful sink. Neither file was wrong on its own reading.

### the osc panel

`osc-watch` cannot run while a show holds the port, which is exactly when the
question gets asked — so the viewer grew a band for it (`osc`, or `[o]`). It
shows every address arriving with what the map does with it, everything going
out, and the last `/scenes/{name}` the app announced.

Two decisions in it:

**Rows are discovered, never listed.** Nothing in the panel knows what
addresses exist, so a build that nests its uniforms differently or a scene
publishing controls of its own appears with no code edited. A list written from
the documentation would have hidden the one case worth seeing, since the
addresses are not in the documentation at all.

**It never says "connected".** Over UDP there is no such thing, and `osc.py`
already said so in a comment explaining why `SynesthesiaLink` offers no
`is_connected`. So the panel reports three separable facts — when something
last arrived, whether the socket took our sends, and whether the app has
announced a scene — and a rig sending happily to an app that is not listening
reads as "out: sending, in: nothing heard". That is the failure that costs an
evening, and a single green lamp would have hidden it.

`SynesthesiaLink._send` now takes an address and values rather than a finished
packet, so there is one place that knows *what* is going out as well as that
something is. `on_send` hangs off it, fenced, because an observer on a
diagnostic on a decoration must not be able to stop a send.

`osc-watch` gained the same check offline, in both directions: which binding
takes each arriving address, and which enabled bindings matched nothing that
arrived. The second is the one that bites — a binding aimed at an address this
build does not send is invisible from the rig, because it looks exactly like a
visualiser that is switched off.

---

### the audio meter, and two things it caught

`pattern audio`: one cue per channel of the bus, built from the channel table
rather than a list beside it. An instrument rather than a look - the analysis
wire has several failures that all present as "the lights are not moving", and
one that presents as nothing at all, which is a uniform the app publishes and
never fills.

Three states drawn apart, and the third is the reason it exists: a value
(brightness, in the family's colour), a peak (held above it and decaying,
because a transient is two frames wide and flickers too fast to size), and
**not wired** - a dim amber breath that no real reading looks like. A channel
nobody is filling and a channel sitting at zero are the same number and
completely different problems, and drawing both as black would have made this
useless for its one job.

Not spatial. A meter filling up the rig would read as a different number on
each device: the obelisk keeps its own 0..43 rather than being squashed into
the show's 0..1, the ring is in literal stage coordinates, the truss is offset
past both.

**It found the layers should never have been in the show.** The first
measurement said every channel was moving, including `high_hits`, which is flat
zero - the three additive hit layers were adding red, green and blue over the
meter. My first fix was to have the surface switch them off on the way in,
which was treating the symptom: on the rig it showed up as the obelisk locked
yellow-green and jittering on *every* cue, not just the meter.

The layers rode over everything. The argument for leaving them patched was the
invisibility guarantee - a layer at black adds nothing - and that holds exactly
until the channels are being fed. A permanent additive overlay across three
devices is an implicit global effect, and "invisible while the input is dead"
is not a property to design around.

So they moved to `config/audio_layers_test.json`, a bench rig that is the only
config declaring any layers, and the show carries none. The capability -
`blend: add`, mods off the bus, `device/*` globs - is unchanged and still
tested; only the patching went. slot_5 went back to a placeholder with them,
since the `hits` cue was a near-black base that existed only to be added to.

**It found a race in the wrapper.** `ShowController.command()` drained the
reply queue on every call - including `expect_reply=False`, which is how the
`audio` lines go out, a couple of hundred a second off the OSC listener's
thread. So any command waiting for a reply had it thrown away by traffic with
nothing to do with it, and the symptom was
`no reply to 'layer hit_obelisk mod level off' within 5.0s` from a command that
had actually worked. Streamed lines no longer touch the queue, and replying
commands take a lock so a streamed write cannot land between a drain and its
read. This was live on the rig, not in a test, and it would have shown up as a
pad occasionally not firing during a set.

Making the meter the config's opening pattern broke thirty-nine tests, all of
which had been inheriting the machine under test from whatever the shared
config happened to open on. `ShowController` grew `pattern` / `state` kwargs -
which the executable and the viewer already had - and those tests now say which
machine they mean. The viewer passes them down too, so the window opens on the
look asked for rather than switching to it after start, which used to show the
config's look for a beat first.

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
| `cbfd923` | `elink`: a desk drives a relic over USB, in cue and pixel modes |
| `dcac323` | it runs on the obelisk; reflashing needs no button; C++23 fallout |
| `4b9fce0` | devices and environments, a panel per device, the down-strip fix |

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
- all twelve states are announced in table order, and every placeholder renders
  visibly and differently from the others
- `BEAT` lines do not accumulate in the controller's event list, the same way
  frame lines do not
- a bad tempo, and a MIDI port that is not there, are both rejected without
  killing the show
- the viewer's tempo readout, its tap/bpm/division buttons and the master
  brightness slider all take, and the slider and the arrow keys stay in step
- the beat look fires on every beat, and opens on the attack and decay its line
  in the cue list gives it - 0.15/0.60 - which stay live knobs at the desk
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

And, for devices and environments:

- all five configs written before environments load unchanged, as one device at
  the origin, resolving to the same fixtures and channels they always did
- `config/mythos26.json` resolves to two devices and 354 fixtures, and **python
  and the executable agree on every one of them**
- one pattern renders across both: `obelisk_seasons` on the environment puts
  four distinct seasons on the obelisk's four faces *and* colour on the pars,
  which is the check that the placements did not flatten the sculpture
- an environment round-trips through python without needing the device files it
  was built from
- the obelisk's eight runs each cover y 0..42 with 43 pixels, up and down alike
- the viewer builds one panel per device, tiles them by `view_scale`, and slices
  each frame to the right one
- on real hardware: the two-device show opened the obelisk on COM4 and the pars
  on COM3 together, and the sculpture confirmed the takeover

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

And, for the OSC link out to a visualiser:

- the packet is checked byte for byte — address and tag blocks null-terminated
  and padded to four, floats big-endian, every block four-byte aligned across a
  range of address lengths. OSC is unacknowledged, so a malformed packet is not
  rejected, it is *ignored*: these tests are the only place the format gets to
  be wrong out loud
- gamma is undone rather than applied again: 128 sends 0.731, not 0.22
- `--separate` sends three messages on `/r`, `/g` and `/b`, packed sends one
- **caught on the wire**, against a UDP receiver on loopback rather than against
  the app: `osc --test` puts 148 well-formed messages on the socket in 5s, and
  `osc ../config/synesthesia_test.json --device synesthesia` puts 179 in 6s
  whose values track the palette wave the show is rendering
- a fixture index past the end of the frame says so once, out loud, instead of
  silently sending nothing and reporting "no frames arrived"
- the report prints on Ctrl-C, which is how a set actually ends
- an address that does not resolve is one sentence, not a traceback
- `eclipse_chroma_key`'s shader compiles clean under a real GLSL ES 3.00
  compiler with the Synesthesia helpers stubbed, and renders as designed against
  colour bars: in chroma mode the bar matching the key keeps its colour the
  whole length of its brightness ramp, every other bar goes monochrome, and with
  no media the hue-wheel fallback keys exactly one wedge

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
python -m unittest discover -s python/tests -v      # 161 tests
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
- **Bars** — the clock keeps one, but an assumed one: four beats long, starting
  at the last downbeat it was *told* about, because nothing upstream reliably
  says where a bar begins. Four is not a knob, and a source in three has no way
  to say so. `midi align` on the one is the whole of the fix.
- **Stereo VU** — only the mono signals are read. The mapping sends left and
  right separately, which a rig split into two halves could use.
- **The viewer draws discs, not beams.** Fixtures with a real position in space
  are drawn as a flat 2D scatter; there is no notion of where a light is
  pointing, so it cannot show you a stage wash. It answers "is each fixture
  doing the right thing", not "what will the room look like".
- **The viewer's layout is 2D only.** `position` takes x and y; a rig hung at
  different heights and depths flattens.
- **Timing on the relic is unmeasured.** It accepts frames at 30fps and says so;
  nothing has measured what it actually draws, or what `DEPLOYMENT 1` buys.
- **The sketch is built by hand, not by CI.** `desktop/build.ps1` does not touch
  it; it takes arduino-cli in WSL and `tools/patch-libraries.sh` first.
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
7. **What does it want to run?** The link works and the picture is right, so the
   open question is no longer whether but what: the obelisk has three looks of
   its own and the desk can put anything on it, including `mythos26` and the
   beat. Worth deciding what a show on this sculpture actually is before
   building more machinery for it.
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
