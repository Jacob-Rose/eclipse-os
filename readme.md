

### Prereqs

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

Welp, found graphics to be quite difficult, but building with this library means were locked into Arduino IDE.
1. Using Adafruit Libraries for RP2040
2. Get GC9A01 that uses Adafruit TFT


## Wiring

IO:

4 Button (2 wire + pullup resistor)
1 GC9A01 Screen (SPI Interface)
1 WS2812B [LED_RINGS]
4 WS2812B Outputs [BODY_LEDS, GLASSES_LEDS, MONOWIRE_BUTTONS (not WS2812B, just using connector), unused]

Bat -> 4 Button + 2 Button from external