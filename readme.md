
### Introduction

Hello World!

Don't know how you stumbled upon this but I appreciate you checking it out and hope it can be at least a jumping off point for getting Adafruit microcontrollers into a fully-finished prop (or as I like to call it, a relic).

#### What is this?

A custom microcontroller prop's code.


### Legalese
I take great pride in my work. With much of this work being the "game-systems" side of things, I found showing/reproing this full stack setup from the top down was vital for me to continue redoing/expanding these types of projects. I understand the importance of open source work and sharing knowledge, which is why this is provided. 

My wish though is that you do not remake what I have made, but you make something new. I hope this is mearly a reference for the work and steps, but please, don't be a script kiddie, make something new from this, I made it so easy for ya.

License Info is located on license.md. GNU GPLv3

### Prereqs

Welp, found graphics to be quite difficult using the provided pico library, but building with Adafruit/Arduino libraries means were locked into Arduino IDE for now and makes this not work out of the box which was a goal.

#### Arduino IDE Setup (easiest)

1. Install Arduino IDE
2. Install RP2040 support for Arduino IDE
   > Arduino-Pico Project + Install Instructions [Github](https://github.com/earlephilhower/arduino-pico) 
3. Get Adafruit GC9A01 library in Arduino IDE
   > Can be downloaded + auto-setup in Arduino IDE Library Manager
4. Get latest version of FastLED from Github and install in library
   >[!WARNING] 
   > FastLED 3.6.0 that is the latest obtainable in Arduino IDE Library Manager currently was shown to have issues with Arduino Pico that were fixed in later patches of FastLED that are not on official 3.6.0 release yet.
    
    This makes it a requirement to download FastLED manually, but this is rather easy
   1. Download Latest [FastLED](https://github.com/FastLED/FastLED) and paste the internal folder in your Arduino user library
      - User Arduino Library is located in your Documents folder at %USER%/Documents/Arduino
      - Still will give pragma warning for my current wire setup with ``#      pragma message "Forcing software SPI - no hardware SPI for you!"``. I have not personally found any impact as the FPS is still incredibly good, but this might impact performance. 
         >I have gone in to disable this call manually on my FastLED library.


#### CMAKE Instructions
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

### Hardware Required

A note here, all of this hardware is rather modular and you can pick/choose your usage case.

1. Adafruit Feather RP2040 [(from Adafruit Site)](https://www.adafruit.com/product/4884)
2. GC9A01 Display [(the one I used)](https://a.co/d/eZSs6bW)
3. LED Ring [(the one I used)](https://a.co/d/hDfiO7c)
   1. note... I cut the inner and outer rings, so we really only use the 3rd and 4th layer. Will find other use for the bigger ones
4. Power Latching Push Button [(the one I used)](https://a.co/d/b0Frmu7)
5. Interface Buttons [(the one I used)](https://a.co/d/9bYm0rQ)
6. Battery [(the one I used)](https://a.co/d/8RQDxHi)
   1. make sure if you get a different battery to ensure the polarity is the same as the Feather board
7. WS2812B M/F Connectors and JST M/F connectors


#### Wiring + IO

Used hardware:
- 4 Button (2 wire + pullup resistor)
- 1 GC9A01 Screen (SPI Interface)
- 1 WS2812B Output [LED_RINGS]
- 2 External WS2812B Outputs [BODY_LEDS, GLASSES_LEDS]
- 1 Psuedo WS2812B Input (WS2812B connector used for buttons)

Not gonna give a whole overview, but just highlights for important knoweldge and how I did things in my setup.

1. Battery | The push button was wired to an Input and Output JST connector that the battery and the board connect through, giving us an external on/off switch.
2. All pins on Adafruit Feather RP2040 from my findings support pull up resistors on nearly all GPIO pins (used for button inputs).


## See Also
Related systems include [jelly_main](https://github.com/Jacob-Rose/pico-jelly) and [afterglow](https://github.com/Jacob-Rose/afterglow)

Making images to c code: https://github.com/bitbank2/image_to_c