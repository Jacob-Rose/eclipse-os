
# Introduction

Hello World!

## What is this?

A custom microcontroller prop's code. This logic is made to be a universal hook in for many future projects, where this can be the starting library/template


The library code is currently broken into three modules:

### ecore
contains core system functionality and types. nothing in ecore has any dependencies

### eanim
contains advanced generator system for creating led animations

### eio
contains io device wrappers



## Legalese
I take great pride in my work. With much of this work being the "game-systems" side of things, I found showing/reproing this full stack setup from the top down was vital for me to continue redoing/expanding these types of projects. I understand the importance of open source work and sharing knowledge, which is why this is provided.

My wish though is that you do not remake what I have made, but you make something new. I hope this is mearly a reference for the work and steps, but please, don't be a script kiddie, make something new from this, I made it so easy for ya.

I do not own any images provided in this.

License Info is located in the [license file](license.md). GNU GPLv3

# Setup + Guide

Welp, found graphics to be quite difficult using the provided pico library, but building with Adafruit/Arduino libraries means were locked into Arduino IDE for now and makes this not support cleaner coding with cmake.

## Arduino IDE Setup (easiest)

1. Install Arduino IDE
2. Install RP2040 support for Arduino IDE
   >  [Arduino-Pico GitHub w/ Install Instructions](https://github.com/earlephilhower/arduino-pico) 
3. Get Adafruit GC9A01 and AnimatedGif libraries in Arduino IDE
   > Can be downloaded + auto-setup in Arduino IDE Library Manager


### Raspberry Pico / This software Programming Tips
   - A picoprobe might be preferred as there is currently no way to serial output if a crash occurs. This makes a lot of sense as well an unhandled exception could occur. There is a plan to address in the [todos readme]()


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