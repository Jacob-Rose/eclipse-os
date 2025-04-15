
# Introduction

Welcome to Eclipse OS, a modern, modular, and scalable library for controlling Adressable LEDs in a performant yet convinent manner. 

There are a lot of useful structures/classes, paradigms, and content all developed together. Hopefully you can extend to suit your needs.=

Written in C++

## Project Goal

The goal of the project comes from a need to generate unique patterns with controls similar to DMX (and hopefully compatible with DMX soon), but allowing us to create configurations at the lower level, create our knobs and controls accesible and custom per device setup, while letting maximum flexibility in sharing code among projects. This system promotes scalable development and lets me expand the featureset gradually.

# Features

## Core Library

The library is broken down in a simple, modular setup to allow selective usage as best as possible. Most modules are dependent on ecore.

### ecore
contains core system functionality and types. nothing in ecore has any dependencies

### eanim
contains advanced generator system for creating led animations

### eio
contains io device wrappers

### esm
a simple generic state machine

### external
gathered from other projects, possible conversions. See credits on respective files.


## Kits

These are more content specific and usually just a lot of cool presets. They are made as generically as possible.


## Legalese
I take great pride in my work. With much of this work being the "game-systems" side of things, I found showing/reproing this full stack setup from the top down was vital for me to continue redoing/expanding these types of projects. I understand the importance of open source work and sharing knowledge, which is why this is provided.

My wish though is that you do not remake what I have made, but you make something new. I hope this is mearly a reference for the work and steps, but please, don't be a script kiddie, make something new from this, I made it so easy for ya.

I do not own any images provided in this.

License Info is located in the [license file](license.md). GNU GPLv3

# Setup + Guide


## Arduino IDE Setup (easiest)

1. Install Arduino IDE
2. Install RP2040 support for Arduino IDE
   >  [Arduino-Pico GitHub w/ Install Instructions](https://github.com/earlephilhower/arduino-pico) 
3. Get Adafruit GC9A01 and AnimatedGif libraries in Arduino IDE
   > Can be downloaded + auto-setup in Arduino IDE Library Manager
4. SPI Fixes (IMPORTANT)
   - SPI had some bs fixes. I just modified these. Try and see if necessary as they maybe fix this soon.
      - SPI.h: line 50 -> convert byte to uint_8
      - SPIHelper.h: line 6 -> needed to add #pragma once


### Raspberry Pico / This software Programming Tips
   - A picoprobe might be preferred as there is currently no way to serial output if a crash occurs. This makes a lot of sense as well an unhandled exception could occur. There is a plan to address in the [todos readme](todo.md)


#### CMAKE Instructions [incomplete]
Hopefully in future we can get these libraries linked with CMake to support this dev out of the box and improve VS support and parsing. CMake is started but untested and not working currently.
<details>

<summary>CMake Setup Instructions</summary>
~~1. Install CMake
https://cmake.org/download/~~

~~2. Install Pico SDK using Installer
    - make sure to set up enviorment vars here
https://github.com/raspberrypi/pico-setup-windows~~

~~3. Install Arduino IDE
https://www.arduino.cc/en/software~~

~~4. Install RP2040 Support with Arduino IDE
https://github.com/earlephilhower/arduino-pico~~

~~5. Install Arduino CMake
https://github.com/queezythegreat/arduino-cmake/tree/master~~
</details>


## See Also
Related systems include [jelly_main](https://github.com/Jacob-Rose/pico-jelly) and [afterglow](https://github.com/Jacob-Rose/afterglow)

Making images to c code: https://javl.github.io/image2cpp/


## Baking Images

Ok, so this sucks a bit, gonna be honest. This is the worst code of the project and im sorry, but it does the job.

I made images as small as possible, but then had major issues with using the transparency layers and optimized gifs. 

Right now, the process I have is to go here. https://ezgif.com/repair/

Click Gifscicle unoptimize as well as ImageMagick coalesce. This can the size a lot. (+62.33%) Maybe you can just fix the code and do a PR if you want!
I might at some point, but im also lazy.