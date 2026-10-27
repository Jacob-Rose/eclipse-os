
# Introduction

Welcome to Eclipse OS, a modular C++ library for driving addressable LEDs and the things around them - DMX fixtures, MIDI controllers, a desk over USB - on an RP2040 or on a desktop, from one codebase.

There are a lot of useful structures/classes, paradigms, and content all developed together. Hopefully you can extend to suit your needs.

## Project Goal

The goal of the project comes from a need to generate unique patterns with controls similar to DMX, but allowing us to create configurations at the lower level, create our knobs and controls accessible and custom per device setup, while letting maximum flexibility in sharing code among projects. This system promotes scalable development and lets me expand the featureset gradually. DMX is no longer a hope: a relic can speak it in both directions, and the same looks run on a real rig from a desk.

This aint complex code, read it, learn about it. I write code like its made to not just work well, but be read and understood.

I see this as a generic abstract layer to handle modules for these microcontrollers without overhead. An all in one system for these microcontroller devices.

## Where it sits

Eclipse OS is a middleman. Upstream of it sit the tools a show is already run from - TouchDesigner, DJ software, a visualiser, a lighting desk - and it speaks their languages: DMX in and out, MIDI notes and beat clock, OSC levels, a USB link. Downstream sit the lamps. What it adds in between is the thing none of those tools do well: procedural, generative patterns running on a cheap microcontroller, with their knobs reflected out so the upstream tool can turn them. TouchDesigner can drive a relic's looks and read its state; a relic can equally run on its own, with no computer in the room, from the same code.

# The Library

The library is an Arduino library at `lib/` (sources under `lib/src`), and everything in it is a module: a relic takes the ones it needs and the rest never compile into it. Every module depends on `ecore` and nothing depends on a module above it in this table, so you can stop at any row. A sketch includes `<eclipse-os.h>` once, which puts the library on the include path, and then names modules directly: `<ecore/hsv.h>`, `<eanim/generator_hsv.h>`, `<edmx/fixture.h>`.

| module | what it gives you | depends on | needs Arduino? |
| ------ | ----------------- | ---------- | -------------- |
| `ecore` | the types everything shares | nothing | no |
| `eio` | strips, nodes, buttons, screens | ecore, elink | the pixel and screen drivers only |
| `eanim` | looks: generators, curves, noise, particles | ecore, eio | no |
| `esm` | state machines with blends between looks | ecore, eio, eanim | no |
| `elink` | the USB wire between a desk and a relic | ecore, eio | the serial transport only |
| `edmx` | DMX universes, fixtures, output and input | ecore | the UART transports only |
| `ebeat` | the beat grid and the hits off it | ecore | no |
| `emidi` | MIDI messages, clock, bindings | ecore, ebeat | the UART and USB transports only |
| `enet` | datagram transports: any Arduino UDP, a host socket | ecore | the Arduino UDP wrapper only |
| `eosc` | OSC messages, packets, address globs, bindings | ecore, enet | no |
| `ewifi` | a WiFi manager (experimental) | ecore | yes |
| `emqtt` | Home Assistant over MQTT (experimental) | ecore | yes |
| `external` | vendored third-party code | - | - |

"Needs Arduino" means a file guarded by `USE_ARDUINO`. With it off, the same sources build on a desktop: that is how `tests/run.sh` runs the library on a host and how the desk app links it. The headers say how to use each piece; the design reasoning lives in the commit history, not the comments.

### ecore
The foundation, with no dependencies of its own.
- **HSV colour** in fixed point (`fp.h`, `hsv.h`): a 2^14 scale so per-pixel math is shifts and multiplies on a chip with no FPU, with blends that take the short way round the hue circle.
- **Properties and reflection** (`property.h`, `reflectable.h`): anything with knobs implements one `reflect()` and hands out floats, bools, colours and curves by name, with a role for the clocks a desk uses to run a copy of a look in step.
- **Colour mixing in float** (`color_mix.h`): RGB and HSV blends the way a lamp mixes, for cross-fading whole frames on a desk or a look with the budget; `HSV::blend` stays the fixed-point per-pixel one.
- **Bindings** (`binding.h`): the half every wire shares. `landUnit` puts a 0..1 value on a property in that property's own range, and `BindingRule` decides when a stream of numbers acts: every change, rate-limited with a keepalive, or once on a rising edge. MIDI, DMX and OSC bindings all end in the same place.
- **Names** (`name.h`): hashed strings that are the same number on the host and the relic, with a dotted hierarchy for trigger tags.
- Logging, coordinates, ranges, timers, the `Tickable` interface, a small math library, and a host shim so all of it builds without Arduino.

### eanim
How a look is made.
- `GeneratorHSV` is the base of every look: a colour per node, a clock, and its knobs. `LayeredGeneratorHSV` stacks looks, bottom first, as the one way looks combine.
- `AutomationCurve` and its trigger play a keyframed shape from an impulse, so a hit on the beat has an envelope rather than a number.
- Noise fields (FastNoiseLite), LFOs, float processors (spring, momentum) and a fixed-pool particle system, all of which reflect their clocks so a desk can match them.
- `GeneratorRegistry` (`registry.h`): looks by name. Content registers what it ships (`src/relics/registry.cpp` does it for every relic look), a desk lists and makes them, a relic can pick one by name off a wire.

### esm
A state machine where each state runs a look, and the machine cross-fades between them with a named blend: `cut`, `crossfade`, `rgb`, `dip`, `wipe`. A state can be cued mid-fade and the picture never snaps back.

### eio
The hardware a look draws on. `HSVStrip` wraps a NeoPixel strip in HSV; segments and nodes give every pixel a place in a coordinate space, and a *space* says which object it belongs to - ring, obelisk, truss - so a look can treat a sculpture as a sculpture. Buttons, a round screen with GIF playback, and `RelicCore`, the program a relic runs.

### elink
The wire between a desk and a relic over the relic's own USB cable. One frame format, compiled into both ends: a desk can stream a relic's pixels for a show or just name the look it should run, and the relic takes its own pixels back the moment the stream stops. See [desktop/readme.md](desktop/readme.md#over-usb).

### edmx
DMX for both the desk and a relic. A `ChannelBuffer` (a `DmxUniverse` is the 512-channel case), the `DmxOutput` interface a transport implements, and the fixture model: profiles, the patch, and rendering HSV into channels with dimmer, parked channels and gamma. `ChannelBinding` drives a look's reflected properties from incoming DMX, so a conventional lighting desk can turn its knobs. On the RP2040, `UartDmxOutput` sends a universe from a UART by DMA and `UartDmxInput` receives one, through an RS-485 transceiver whose pins go in `relics/wiring.h`. The desk's USB widgets implement the same interface.

### ebeat
The beat, whatever says where it is. `BeatClock` is the grid everything pulses to: it predicts between beats, free-runs through a dropout, counts beats rather than messages (a dropped message moves the count by two, a duplicate by none), and can be led by a latency so the light lands on the music. `TriggerRack` turns it into four hits per beat - bar, half, beat, double - decided once a frame for every look, so two looks on the same rate hit on the same frame. MIDI clock, MIDI notes, an OSC detector and a tap all feed the same clock. `AudioLevel` (`level_bus.h`) is the audio bus beside it: named 0..1 channels an analyser or a VU note fills and a look reads, each holding briefly and then fading so a dropped link goes dark. `shared.h` holds the one clock, rack and bus a process has, and `nowSeconds()` to read them against. The desk's beat clock, triggers and bus are these classes.

### emidi
MIDI for both. The message model, a byte-stream `Parser` (running status, interleaved realtime), a `ClockFollower` for 0xF8 beat clock, `BeatNotes` for the beat, tempo and level notes DJ software sends (Mixxx's numbering by default), and `Binding` to land controllers on reflected properties and notes on trigger tags. `MidiPort` pumps a `Transport` each tick; on the RP2040 that is `UartMidiTransport` (DIN, 31250 baud) or `UsbMidiTransport` (a class-compliant USB MIDI device, needs the TinyUSB stack). The desk's MIDI input is these classes with a platform port underneath.

### enet
The network as the library sees it: a `DatagramTransport` that a protocol module pumps, and the sockets behind it. `ArduinoUdp` wraps any Arduino `UDP` - a `WiFiUDP` on a Pico W today, an `EthernetUDP` on a wired board tomorrow, the same code - and `HostUdp` is a BSD or winsock socket for a desk. Joining a network is a different job and stays with `ewifi`.

### eosc
Open Sound Control for both. A message model, a packet codec (bundles unpacked, malformed input tolerated), address matching as a glob (`*BassLevel*` finds the band wherever a visualiser nests it), and `Binding` on the shared `ecore::BindingRule`: a value or a rising edge, rate-limited, landed on a reflected property or fired as a trigger tag. `OscPort` pumps any `enet` transport. On a Pico W that is TouchDesigner or a visualiser driving a relic's knobs over WiFi with no desk in the room; the desk's own OSC in and out are these classes behind its `osc` commands.

### ewifi and emqtt (experimental)
A WiFi manager that keeps secrets out of the tree, and a Home Assistant integration over MQTT that advertises a relic's controls by discovery. Only the whiteboard uses them today; `tools/check-ha.py` proves the integration from your machine after a flash.

### external
Vendored third-party code, each with its licence beside it: FastNoiseLite, an easing library, homog2d, and AnimatedGIF with a one-block fix for the arduino-pico core (`lib/src/external/AnimatedGIF/README.eclipse-os.md`).

## Relics

A relic is one sculpture's program: its wiring, its IO layer, its state machine and its looks, under `src/relics`. The obelisk and the whiteboard are the ones built and flashed today; the jacket, pendant and gloves are earlier builds kept in the tree. Where a sculpture's pixels, DMX port or MIDI jacks are plugged in is one line per relic in `src/relics/wiring.h`, and nowhere else.

Looks that are content rather than a sculpture live here too: `src/relics/mythos26` is the mythos26 show's looks, `src/relics/generic` the audio meter, and `src/relics/registry.cpp` puts every relic look into the library's registry by name. The desk builds its pattern list from that registry, so a look added there is on the desk, in its python tools and on any relic that asks for it by name, with nothing else edited.

## Kits

Content rather than machinery: palettes, effects and pattern presets made as generically as possible, so they work from a 1d strip on a ceiling to a 2d sculpture depending on the underlying kit.

## The desk: eclipse-dmx

`desktop/` is a show runner that renders the same looks onto a DMX rig through a USB widget, drives a relic over `elink`, follows a beat from MIDI or audio, and has a viewer, pads and a curve editor in python. It is the middleman in practice: TouchDesigner, Mixxx or a visualiser on one side, relics and fixtures on the other. Its own code is the `edesk` namespace; everything it shares with the relics is the library above. See [desktop/readme.md](desktop/readme.md).

## Tests

`tests/run.sh` builds the portable library with a host compiler and runs `tests/*.cpp`, a dependency-free `TEST`/`CHECK` harness. The fixed-point and name headers also pin their contracts with `static_assert`. The desk has its own self-tests and a python suite; see its readme.

## Legalese
I take great pride in my work. With much of this work being the "game-systems" side of things, I found showing/reproing this full stack setup from the top down was vital for me to continue redoing/expanding these types of projects. I understand the importance of open source work and sharing knowledge, which is why this is provided.

My wish though is that you do not remake what I have made, but you make something new. I hope this is mearly a reference for the work and steps, but please, don't be a script kiddie, make something new from this, I made it so easy for ya.

I do not own any image binaries provided in this source code.

License Info is located in the [license file](license.md). GNU GPLv3

# Setup + Guide
A lot of work has gone to easy deployment and making this system less fragile. The firmware builds with arduino-cli on Linux, in WSL, or natively on Windows - the same commands everywhere.

## Arduino CLI (recommended)

If terminals are scary, the IDE instructions should work. I recommend the CLI.

### First-time setup

The short way: `sketch.yaml` carries a build profile with the board, the exact
core and library versions, and this repo's own library by path. One command
fetches all of it into a profile-local folder, installs nothing globally, and
needs nothing re-installed after a change:

```sh
arduino-cli compile --profile obelisk
```

The long way, if you want the core and libraries in your global Arduino folder
(this is what the build scripts use). Last verified with **arduino-cli 1.5.1**
and the versions noted below. The library is C++20.

1. Add the arduino-pico (earlephilhower) board index and install the core:

   ```sh
   arduino-cli config add board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
   arduino-cli core update-index
   arduino-cli core install rp2040:rp2040          # verified w/ 6.3.1
   ```

2. Install the libraries the sketch pulls in:

   ```sh
   arduino-cli lib install "Adafruit GC9A01A" "Adafruit NeoPixel" "FastLED" "PubSubClient"
   # AnimatedGIF is vendored under lib/src/external and needs no install or patch
   # eclipse-os itself is never installed: the scripts pass lib/ to the build by path
   ```

   (Adafruit GFX + BusIO come along as dependencies.)

3. Create your secrets file from the template - only needed for a relic that
   talks to Home Assistant (see **Which relic** below); the obelisk builds
   without one:

   ```sh
   cp secrets.h.example secrets.h
   ```

The build scripts look for `arduino-cli` on `PATH` and in `~/.local/bin`. On
Windows, `winget install ArduinoSA.CLI` and a one-line wrapper script in
`~/.local/bin` is enough for the same scripts to run from Git Bash.

### Which relic

A board is flashed for exactly one sculpture, and the build scripts say which:

```sh
./upload.sh whiteboard deploy    # the string lights on the wall, for a month
./upload.sh                      # the obelisk, dev - the same as no words ever meant
```

The words (`obelisk`/`whiteboard`, `deploy`/`dev`, `-p PORT`) are the same for
`compile.sh`, `upload.sh`, `monitor.sh` and `tools/build-firmware.sh`, in any
order - `./compile.sh --help` lists them. They become `-D`s on the whole build
(`tools/firmware-env.sh`), and the `#define`s at the top of `eclipse-os.ino`
are only the defaults behind them. Everything else follows from the relic: the
obelisk has no network and needs no `secrets.h`; the whiteboard cannot work
without both, and the scripts say so before the compiler does.

Where a sculpture's pixels are plugged in - pin, length, colour order - is one
line per relic in `src/relics/wiring.h`, and nowhere else. The scripts print
that line before every build (`wiring: GP5 x 300, NEO_BGR + NEO_KHZ800`) and a
dev build prints it in its boot banner, because a board built for the wrong
strip is a dark strip with nothing in the log.

**After every flash of a relic that talks to Home Assistant, run
`tools/check-ha.py`.** It does from your machine what HA does: logs into the
broker with `secrets.h`, reads the device's discovery configs, drives every
command they advertise and expects the relic to echo the state back. `HA
CHECK PASS` means HA can see *and reach* it; anything less names the broken
thing. `--tidy` clears discovery left behind by an older build of the same
board (a renamed device id), which otherwise makes HA reject the new one as a
duplicate. Neither failure shows up in the serial monitor, which is why this
exists.

```sh
./upload.sh whiteboard deploy && tools/check-ha.py
```

`deploy` is `DEPLOYMENT 1` - the tighter loop - and `DEBUG_LOGGING_ENABLED 0`
for the library too.

`USE_RELIC_LINK` is the desk's cable - see
[desktop/readme.md](desktop/readme.md#over-usb). Leave it on: a laptop can then
take the sculpture's pixels for a show and hand them back, and typing commands
into a serial monitor still works exactly as before.

### Build / upload / monitor

1. `tools/build-firmware.sh` -> compile and leave a `.uf2` in `build-firmware/`,
   named for what it is: `eclipse-os.whiteboard.deploy.uf2`
2. `compile.sh` -> compile for the Raspberry Pi Pico W
3. `upload.sh` -> compile *and* upload to the first found device (or `-p PORT`). One
   step, because a bare `arduino-cli upload` sends whatever the build cache last
   held, which may be the other sculpture
4. `monitor.sh` -> view serial out of the first found device

To get the `.uf2` onto the board, either drop it on the `RPI-RP2` drive, or -
if the board is already running a build with the link in it - put it into its
bootloader from the desk without touching the BOOTSEL button:

```sh
desktop/build/eclipse-dmx --reboot-bootsel
```

### Host builds

The library builds on a desktop compiler with `USE_ARDUINO=0`, which is what
`tests/run.sh` and the desk's CMake build use. The firmware itself is built
only through arduino-cli.

## Arduino IDE Setup (alternative)

1. Install Arduino IDE 2
2. Install RP2040 support for Arduino IDE
   >  [Arduino-Pico GitHub w/ Install Instructions](https://github.com/earlephilhower/arduino-pico)
3. Install the Adafruit GC9A01A, Adafruit NeoPixel, FastLED and PubSubClient libraries from the Library Manager
4. Make the repo's `lib/` folder visible to the IDE. Recent IDE 2 versions read
   `sketch.yaml` and find it there; otherwise link it once into your Arduino
   libraries folder (`ln -s <repo>/lib ~/Arduino/libraries/eclipse-os`, or
   `mklink /D` on Windows). A link means an edit in the repo is live on the
   next build - nothing to re-install.
5. Pick the relic with the `#define`s at the top of `eclipse-os.ino`; the IDE
   has no way to pass the `-D` flags the scripts use.

## Library Patches

None. Two used to be needed, and both are worth knowing about:

- **AnimatedGIF** skips `#include <Arduino.h>` whenever `PICO_BUILD` is defined,
  which the arduino-pico core does, and then calls `millis()` and `delay()`
  undeclared. Still the case upstream as of 2.2.0, so the library is vendored
  with the fix in the tree: `lib/src/external/AnimatedGIF`, with a
  `README.eclipse-os.md` beside it. Do not install it from the library manager;
  every include names the vendored copy, so an installed one is never used.

- **The SPI patches** existed because `eio/relic.h` did `using namespace std;` at
  global scope, which put `std::byte` in scope for every translation unit that
  included it - and then Arduino's own `byte` was ambiguous, with the error
  appearing deep inside `SPI.h` where nothing looked wrong. The core builds at
  `-std=gnu++23`, where `std` also has `lerp`, so the headers here never open
  `std` and every `lerp` is `std::lerp`. See the note in `lib/src/ecore/name.h`.

### Raspberry Pico / This software Programming Tips

- A picoprobe might be preferred, although I got through all of development with only serial output (hence the nice logging library) as there is currently no way to serial output if a crash occurs. There is a plan to address in the [todos readme](todo.md).

## See Also

Related systems include [jelly_main](https://github.com/Jacob-Rose/pico-jelly) and [afterglow](https://github.com/Jacob-Rose/afterglow)

Making images to c code: https://javl.github.io/image2cpp/

## Baking Images

Ok, so this sucks a bit, gonna be honest. This is the worst code of the project and im sorry, but it does the job.

I made images as small as possible, but then had major issues with using the transparency layers and optimized gifs.

Right now, the process I have is to go here. https://ezgif.com/repair/

Click Gifscicle unoptimize as well as ImageMagick coalesce. This can the size a lot. (+62.33%) Maybe you can just fix the code and do a PR if you want!
I might at some point, but im also lazy.
