// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "gm.h"

GlobalManager* GlobalManager::singleton = nullptr;

void GlobalManager::initSingleton()
{
    if(singleton == nullptr)
    {
        singleton = new GlobalManager();
        singleton->init();
    } 
}

void GlobalManager::cleanupSingleton()
{
    if(singleton != nullptr)
    {
        singleton->cleanup();
        delete singleton;
        singleton = nullptr;
    }
}


void GlobalManager::init()
{
    GreenButton = std::make_unique<Button>();
    GreenButton->init(GREEN_BUTTON_PIN);

    RedButton = std::make_unique<Button>();
    RedButton->init(RED_BUTTON_PIN);

    BlueButton = std::make_unique<Button>();
    BlueButton->init(BLUE_BUTTON_PIN);

    Screen = std::make_unique<Adafruit_GC9A01A>(SCREEN_CS, SCREEN_DC, SCREEN_MISO, SCREEN_SCLK, SCREEN_RST);
    Screen->begin();

    FastLED.addLeds<NEOPIXEL, RING_LED_PIN>(RingLEDs, RING_LENGTH);
    FastLED.addLeds<NEOPIXEL, BODY_LED_PIN>(OutfitLEDs, BODY_LED_LENGTH);
    FastLED.addLeds<NEOPIXEL, PIN_NEOPIXEL>(BoardLED, 1, 0);
}

void GlobalManager::cleanup()
{
    GreenButton.reset();
    RedButton.reset();
    BlueButton.reset();

    Screen.reset();
}

void GIFDraw_Necklace(GIFDRAW *pDraw)
{
    GlobalManager& GM = GlobalManager::get();
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth + pDraw->iX > SCREEN_WIDTH)
       iWidth = SCREEN_WIDTH - pDraw->iX;
    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line
    if (y >= SCREEN_HEIGHT || pDraw->iX >= SCREEN_HEIGHT || iWidth < 1)
       return; 
    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
      for (x=0; x<iWidth; x++)
      {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }

    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) // if transparency used
    {
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x, iCount;
      pEnd = s + iWidth;
      x = 0;
      iCount = 0; // count non-transparent pixels
      while(x < iWidth)
      {
        c = ucTransparent-1;
        d = usTemp;
        while (c != ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent) // done, stop
          {
            s--; // back up to treat it like transparent
          }
          else // opaque
          {
             *d++ = usPalette[c];
             iCount++;
          }
        } // while looking for opaque pixels
        if (iCount) // any opaque pixels?
        {
            
          GM.Screen->startWrite();
          GM.Screen->setAddrWindow(pDraw->iX+x, y, iCount, 1);
          GM.Screen->writePixels(usTemp, iCount, false, false);
          GM.Screen->endWrite();
          x += iCount;
          iCount = 0;
        }
        // no, look for a run of transparent pixels
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent)
             iCount++;
          else
             s--; 
        }
        if (iCount)
        {
          x += iCount; // skip these
          iCount = 0;
        }
      }
    }
    else
    {
      s = pDraw->pPixels;
      // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
      for (x=0; x<iWidth; x++)
        usTemp[x] = usPalette[*s++];
      GM.Screen->startWrite();
      GM.Screen->setAddrWindow(pDraw->iX, y, iWidth, 1);
      GM.Screen->writePixels(usTemp, iWidth, false, false);
      GM.Screen->endWrite();
    }
    Serial.printf("GIFDraw call finished");
} /* GIFDraw() */