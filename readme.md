
# Introduction

Welcome to Eclipse OS, a modern, modular, and scalable library for controlling Adressable LEDs in a performant yet convinent manner. 

There are a lot of useful structures/classes, paradigms, and content all developed together. Hopefully you can extend to suit your needs.

Written in C++

# Project Purpose

The purpose/goal of this project came from two directions. 

### Firstly... I needed a better addressable LED expanded structure.
I needed to make a generic addressible LED control system that really filled in the gaps left in other libraries and at the same time was easier to get into.

I wanted the full stack, and I wanted to support a system where I never restarted, but I kept expanding the featureset as I used it on new projects.

#### I wanted easy to use core libraries for addressible LEDs that provided
- HSV color space with easy / optimized blending.
- Generalized pattern logic to support hotswapping patterns easily.
- State Machine with transition support.
- 2D Mappable pixels with a generic, expandable pixel mapping structure. 
- Organized tick system with multithreading in mind


### Secondly... I needed a novel way of designing patterns.
The system addresses a need to generate unique patterns in unique contexts with a approach of tunable values and generative data similar to audio synthesizers. This originally was made to be compatible with synthesizer parameters.

I needed to make a cheap and affordable general level system to support LED mapping over many projects, with support for many different mappable values with optimization and ease of use as a high priority

#### System Architecture focus
- Reconfigurability, modularity, and short, easy to comprehend dependency chains.
- Generalized IO system for easier repurposing.
- A pick what you want to use model. Support for minimal usage cases with a good learning curve speed.
- Lightweight as possible, running on a RP2040 (but should work with other chipsets)

## Future Vision

This project is ongoing, and features are added as a project calls for it. Time is limited, but this is a passion, so it is likely going to keep expanding.

#### Large
- FastLED base level. 
   - I had used in the past and had issues with how it assigns buffer and does memory management, but in reevaluation it might be best to open this up to the standard addressible LED library and it has support for nearly every modern strip chipset which I hope to try out soon.
- Network syncing for pattern parameters
   - In many of these softwares, expecially if networked, we might not want to send full buffers, but instead can provide networked parameter syncing for allowing dynamic procedural patterns. This will let this slot in easily for live show deployment. These parameters can be floats, colors, and times.
- Examples of compatibility layers for various softwares like:
   - Ableton Live / Max for Live
   - DMX
   - Serato
   - Touchdesigner or VVVV

#### Medium
- Improved 2d Mapping pipeline / various new contexts.


#### Small
- State Machine improvements.

# Features

## Core Library

The library is broken down in a simple, modular setup to allow selective usage as best as possible. Most modules are dependent on ecore.

### ecore
contains core system functionality and types. nothing in ecore has any dependencies

### eanim
contains advanced generator system for creating led animations. This is a general layer that allows us to hotswap patterns and isolate pattern specific logic from the actual LED strip implementation.

### eio
contains io device wrappers. Currently has decent button layer, decent support for screens

### esm
a simple generic state machine. 

### external
gathered from other projects, possible but minimal modifications. See credits on respective files.


## Kits + Patterns

These are more content specific and usually just a lot of cool presets. They are made as generically as possible, with a flexible archtype system for mapping nodes in whatever context you desire, such as baked 2D mapping positions as used currently.


# Setup + Guide


## Arduino IDE Setup (easiest)

1. Install Arduino IDE
2. Install RP2040 support for Arduino IDE
   >  [Arduino-Pico GitHub w/ Install Instructions](https://github.com/earlephilhower/arduino-pico) 
3. (Optional Screen Support) Get Adafruit GC9A01 and AnimatedGif libraries in Arduino IDE
   > Can be downloaded + auto-setup in Arduino IDE Library Manager

   > you can also avoid this with a #define USE_SCREEN 0 and remove this dependency
4. SPI Fixes (IMPORTANT)
   - SPI had some bs fixes. I just modified these. Try and see if necessary as they maybe fix this soon.
      - SPI.h: line 50 -> convert byte to uint_8
      - SPIHelper.h: line 6 -> needed to add #pragma once


#### CMAKE Instructions [incomplete]
Hopefully in future we can get these libraries linked with CMake to support this dev out of the box and improve VS support and parsing. CMake is started but untested and not working currently. Instructions are for future reference and it is ill-advised to follow this step unless you are planning on taking it all the way home.
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

## Repurposing + Debugging
   - A picoprobe might be preferred as there is currently no way to serial output if a crash occurs or to use breakpoints. This makes a lot of sense as well an unhandled exception could occur. There is a plan to address in the [todos readme](todo.md) Yep, I did this fully serial. There is a nice logging library included based on Unreal Engine log structure.


## See Also
Related systems include [jelly_main](https://github.com/Jacob-Rose/pico-jelly) and [afterglow](https://github.com/Jacob-Rose/afterglow)


## Screens / Baking Images

I pre-apologize for this code being kinda trash. It works, but could use improvements.

Ok, so this sucks a bit, gonna be honest. This is the worst code of the project and im sorry, but it does the job.

I made images as small as possible, but then had major issues with using the transparency layers and optimized gifs. 

Right now, the process I have is to go here. https://ezgif.com/repair/

Click Gifscicle unoptimize as well as ImageMagick coalesce. This can increase the size a lot. (+62.33%). tbh, it works, so i didn't fix. The screen code is some of the weakest code here, but it works, and is fully optional.

Then, you need to convert the code to c++ binary array: https://javl.github.io/image2cpp/

Maybe you can just fix the code and do a PR if you want!
I might at some point, but im also lazy.

## Rights/Licensing
I take great pride in my work. I hope that this library can inspire and cut out a lot of dev time for hobbyist. That being said, please respect the official distributions. Please star if you see use. I am planning on adding a donation link if you are generous enough.

Commecial use is accepted, but please recognize the fact this was made by a single developer for no pay. Please pay it back if you want more support.

Reach out to me directly if you are intererested in collaboration. 

License Info is located in the [license file](license.md). GNU GPLv3

## Contributions are always welcome!

I support and thank anyone who wants to offer PRs and extend this system further. I really think this is a system of solid bones for people to build and flesh out as they see fit. 