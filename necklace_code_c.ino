// Copyright Jake Rose 2024

#include <stdio.h>
#include "pico/stdlib.h"

#include "necklace.h"

#include <Adafruit_NeoPixel.h>

#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

#define RING_LED_PIN D24
#define RING_ONE_LENGTH 12
#define RING_TWO_LENGTH 16
#define RING_LENGTH 28 //RING_ONE_LENGTH + RING_TWO_LENGTH

#define BODY_LED_PIN 6
#define ARM_LED_LENGTH 60
#define WHIP_LED_LENGTH 32
#define BODY_LED_LENGTH 92 //ARM_LED_LENGTH + WHIP_LED_LENGTH

Adafruit_NeoPixel ringsPixels(RING_LENGTH, RING_LED_PIN, NEO_GRB);
Adafruit_NeoPixel bodyPixels(BODY_LED_LENGTH, BODY_LED_PIN, NEO_GRB);
Adafruit_NeoPixel boardPixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

#define TFT_DC D9
#define TFT_CS D10
#define TFT_RST D11
#define TFT_MISO 2 // labeled miso in docs but actually the SDA on my chip, actually GPIO2 on Feather RP2040, 
#define TFT_SCLK 3 // labeled as SCL on my chip

#define GREEN_BUTTON_PIN D12

// Display constructor for primary hardware SPI connection -- the specific
// pins used for writing to the display are unique to each board and are not
// negotiable. "Soft" SPI (using any pins) is an option but performance is
// reduced; it's rarely used, see header file for syntax if needed.
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_MISO, TFT_SCLK, TFT_RST);

void setup() {
  Serial.begin(9600);

  tft.begin();

#if defined(TFT_BL)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Backlight on
#endif // end TFT_BL

  pinMode(GREEN_BUTTON_PIN, INPUT_PULLUP);

  boardPixel.begin();

  boardPixel.setPixelColor(0,10,50,0, 0);

  boardPixel.show();

  ringsPixels.begin();

  for(int i = 0; i < ringsPixels.numPixels(); ++i)
  {
    ringsPixels.setPixelColor(i, 10,100,50);
  }

  bodyPixels.begin();

  for(int i = 0; i < bodyPixels.numPixels(); ++i)
  {
    bodyPixels.setPixelColor(i, 10,100,50);
  }

  tft.fillScreen(GC9A01A_GREEN);

  Serial.println(F("Benchmark                Time (microseconds)"));
  delay(10);
}

void loop(void) {
  // screen code here
  Serial.println(digitalRead(GREEN_BUTTON_PIN));
  //Serial.print(to_us_since_boot(get_absolute_time()));


  ringsPixels.show();

  bodyPixels.show();


  delay(20);
}

void loop1(void)
{
  // led code here
}