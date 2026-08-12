
# Introduction

Welcome to Eclipse OS, a modern, modular, and scalable library for controlling Adressable LEDs in a performant yet convinent manner.

There are a lot of useful structures/classes, paradigms, and content all developed together. Hopefully you can extend to suit your needs.

Written in C++

## Project Goal

The goal of the project comes from a need to generate unique patterns with controls similar to DMX (and hopefully compatible with DMX soon), but allowing us to create configurations at the lower level, create our knobs and controls accesible and custom per device setup, while letting maximum flexibility in sharing code among projects. This system promotes scalable development and lets me expand the featureset gradually.

This aint complex code, read it, learn about it. I write code like its made to not just work well, but be read and understood.

I see this as a generic abstract layer to handle modules for these microcontrollers without overhead. An all in one system for these microcontroller devices.

# Features

## Core Library

The library is broken down in a simple, modular setup to allow selective usage as best as possible. Most modules are dependent on ecore.

### ecore
contains core system functionality and types. nothing in ecore has any dependencies.

#### main features:
- logging system
- coodinate system
- hsv color system
- floating point -> fixed point math conversion

#### additional features:
- small math library
- range struct
- tickable interface
- timer

### eanim
contains advanced generator system for creating led animations generically
   - generators: generalized layer for coordinate -> output (float or color)
      - noise generators (FastNoiseLite)
   - processors: take in a target and perform some algorithm over time to get to it. (ex: spring, momentum, lerp)

### eio
contains io device wrappers.
simple wrapper layers for:
   - buttons
   - gif drawing to screens
   - hsv strip abstraction

### esm
a simple generic state machine. additional layer for hsv blending between states.

### elink
the wire between a desktop and a relic. one frame format, compiled into both
ends, so a laptop can stream a relic's pixels for a show or just name the look
it should run - and the relic takes its own pixels back the moment the stream
stops. see [desktop/readme.md](desktop/readme.md#over-usb).

### ewifi (experimental)
easy to understand wifi manager. easy way to hide secrets.

### emqtt (experimental)
made for home assistant co

### external
gathered from other projects, possible conversions. See credits on respective files.


## Kits

These are more content specific and usually just a lot of cool presets. They are made as generically as possible to work on any project from a 1d strip on the ceiling to a 2d render dependeing on the underlying kit.


## Legalese
I take great pride in my work. With much of this work being the "game-systems" side of things, I found showing/reproing this full stack setup from the top down was vital for me to continue redoing/expanding these types of projects. I understand the importance of open source work and sharing knowledge, which is why this is provided.

My wish though is that you do not remake what I have made, but you make something new. I hope this is mearly a reference for the work and steps, but please, don't be a script kiddie, make something new from this, I made it so easy for ya.

I do not own any image binaries provided in this source code.

License Info is located in the [license file](license.md). GNU GPLv3

# Setup + Guide
A lot of work has gone to easy deployment and making this system less fragile.
To reiterate, this is currently tested in the Arduino ecosystem, with goals to integrate more seamless at some point.

## Arduino CLI (recommended)

If terminals are scary, the ide instructions should work. i recommend the cli.

### Linux

#### First-time setup

You need the [arduino-cli](https://arduino.github.io/arduino-cli/), the RP2040 core, and a handful of libraries. This was last verified with **arduino-cli 1.4.1** and the versions noted below.

1. Add the arduino-pico (earlephilhower) board index and install the core:

   ```sh
   arduino-cli config add board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
   arduino-cli core update-index
   arduino-cli core install rp2040:rp2040          # verified w/ 5.6.0
   ```

2. Install the libraries the sketch pulls in:

   ```sh
   arduino-cli lib install "Adafruit GC9A01A" "Adafruit NeoPixel" "AnimatedGIF" "FastLED" "PubSubClient"
   ```

   (Adafruit GFX + BusIO come along as dependencies.)

3. Create your secrets file from the template — only needed for a relic that
   talks to Home Assistant (see **Which relic** below); the obelisk builds
   without one:

   ```sh
   cp secrets.h.example secrets.h
   ```

4. Apply the library patches below (see **Library Patches**) — currently still required.

#### Which relic

A board is flashed for exactly one sculpture, and one line at the top of
`eclipse-os.ino` says which:

```cpp
#define RELIC RELIC_OBELISK      // or RELIC_WHITEBOARD
```

Everything else follows from it. The obelisk has no network and needs no
`secrets.h`; the whiteboard cannot work without both.

`USE_RELIC_LINK`, just below it, is the desk's cable — see
[desktop/readme.md](desktop/readme.md#over-usb). Leave it on: a laptop can then
take the sculpture's pixels for a show and hand them back, and typing commands
into a serial monitor still works exactly as before. It is deliberately not
gated on `DEPLOYMENT`, unlike the raw serial input it replaced.

#### Build / upload / monitor

1. compile.sh -> compile for raspberry pico w (use as reference, easy to change per chipset)
2. upload.sh -> upload compiled project to first found device
3. monitor.sh -> view serial out of first found device

### Windows

I would recommend just running this in WSL for these tools.

## Arduino IDE Setup (alternative)

1. Install Arduino IDE
2. Install RP2040 support for Arduino IDE
   >  [Arduino-Pico GitHub w/ Install Instructions](https://github.com/earlephilhower/arduino-pico) 
3. Get Adafruit GC9A01 and AnimatedGif libraries in Arduino IDE
   > Can be downloaded + auto-setup in Arduino IDE Library Manager
4. Apply the patches in **Library Patches** below.

## Library Patches (IMPORTANT)

These are hand-edits to installed core/library files (not in this repo, so they don't survive a core/lib reinstall). They're still required as of the versions noted in the setup above — try without them first in case upstream has fixed it.

- **SPI** (`rp2040` core, `.../libraries/SPI/src/`) — `byte` collides with `std::byte` once the project drags `using namespace std;` into scope:
   - `SPI.h` line ~50: change `byte transfer(uint8_t data)` to `uint8_t transfer(uint8_t data)`
   - `SPIHelper.h` line 6: add `#pragma once`
- **AnimatedGIF** (`.../libraries/AnimatedGIF/src/AnimatedGIF.h`) — the current arduino-pico core defines `PICO_BUILD`, so the header skips its `#include <Arduino.h>` while `AnimatedGIF.cpp` still calls `millis()`/`delay()` (`'millis' was not declared in this scope`). Force the include back in by adding, right after the `#else / #include <Arduino.h> / #endif` block near the top:

   ```cpp
   #if defined( ARDUINO )
   #include <Arduino.h>
   #endif
   ```

### Raspberry Pico / This software Programming Tips

- A picoprobe might be preferred, although i got through all of development with only serial output (hence the nice logging library) as there is currently no way to serial output if a crash occurs. This makes a lot of sense as well an unhandled exception could occur. There is a plan to address in the [todos readme](todo.md).

#### CMAKE Instructions [incomplete]

Hopefully in future we can get these libraries linked with CMake to support this dev out of the box and improve VS support and parsing. CMake is started but untested and not working currently.

## See Also

Related systems include [jelly_main](https://github.com/Jacob-Rose/pico-jelly) and [afterglow](https://github.com/Jacob-Rose/afterglow)

Making images to c code: https://javl.github.io/image2cpp/

## Baking Images

Ok, so this sucks a bit, gonna be honest. This is the worst code of the project and im sorry, but it does the job.

I made images as small as possible, but then had major issues with using the transparency layers and optimized gifs.

Right now, the process I have is to go here. https://ezgif.com/repair/

Click Gifscicle unoptimize as well as ImageMagick coalesce. This can the size a lot. (+62.33%) Maybe you can just fix the code and do a PR if you want!
I might at some point, but im also lazy.
