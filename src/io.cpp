// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "io.h"
#include "gm.h"

#include "logging.h"

using namespace j;

void Button::init(uint8_t inPin)
{
    pinMode(inPin, INPUT_PULLUP);
    myPin = inPin;
}

ScreenDrawer::ScreenDrawer()
{

}

void ScreenDrawer::setScreenRef(std::weak_ptr<Adafruit_GC9A01A> inScreenRef)
{
    ScreenRef = inScreenRef;
}

void ScreenDrawer::setCanvasSize(uint16_t x, uint16_t y)
{
    int count = x * y;
    if(colors.size() > count || colors.size() < count)
    {
        colors.resize(count, 0);
    }
    xCanvasSize = x;
    yCanvasSize = y;

    bCanvasEnable = true;
}

/*static*/ void ScreenDrawer::GIFDraw_UpscaleScreen(GIFDRAW* pDraw)
{
    GlobalManager& GM = GlobalManager::get();

    ScreenDrawer* SD = static_cast<ScreenDrawer*>(pDraw->pUser);

    // lil hacky, but it works
    if(pDraw->iX == 0 && pDraw->iY == 0)
    {
        SD->setCanvasSize(pDraw->iWidth, pDraw->iHeight);
    }

    Adafruit_GC9A01A* Screen = SD->ScreenRef.lock().get();
    if(Screen == nullptr)
    {
        return;
    }

    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];

    usPalette = pDraw->pPalette;

    float ScalarFloatY = ((float)SCREEN_HEIGHT) / pDraw->iHeight;

    int startingPixelY = (pDraw->y * ScalarFloatY);
    int endingPixelY = ((pDraw->y + 1) * ScalarFloatY);
    int ySize = endingPixelY - startingPixelY;

    int16_t y = pDraw->iY + (startingPixelY); // current line
    int16_t x = 0;

    if (y >= SCREEN_HEIGHT || pDraw->iX >= SCREEN_HEIGHT || pDraw->iWidth < 1)
       return; 
    s = pDraw->pPixels;

    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
      for (x=0; x < pDraw->iWidth; x++)
      {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }

    Screen->startWrite();
    for(int16_t xPixel = 0; xPixel < pDraw->iWidth; ++xPixel)
    {
        uint8_t c = *s++;
        if (c != pDraw->ucTransparent || c != SD->colors[x * SD->xCanvasSize + y])
        {
            float ScalarFloatX = ((float)SCREEN_WIDTH) / pDraw->iWidth;

            int startingPixelX = (xPixel * ScalarFloatX);
            int endingPixelX = ((xPixel + 1) * ScalarFloatX);
            int xSize = endingPixelX - startingPixelX;

            usTemp[xPixel] = usPalette[c];
            SD->colors[x * SD->xCanvasSize + y] = usTemp[xPixel];
            Screen->writeFillRectPreclipped(startingPixelX, y, xSize, ySize, usTemp[xPixel]);
        }
    }
    Screen->endWrite();
}

HSV::HSV(byte inH, byte inS, byte inV) : h(inH), s(inS), v(inV)
{

}

HSV::HSV() : h(0), s(0), v(0)
{

}


HSVStrip::HSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType) : strip(inLedCount, inLedPin, inPixelType)
{
    strip_HSV = new HSV[inLedCount];

    strip.begin();
}

HSVStrip::~HSVStrip()
{
    delete[] strip_HSV;
}

void HSVStrip::setPixel(uint16_t n, const HSV& color)
{
    strip_HSV[n].h = color.h;
    strip_HSV[n].s = color.s;
    strip_HSV[n].v = color.v;

    updateStripPixel(n);
}

void HSVStrip::setPixel(uint16_t n, uint16_t h, uint8_t s, uint8_t v)
{
    strip_HSV[n].h = h;
    strip_HSV[n].s = s;
    strip_HSV[n].v = v;

    updateStripPixel(n);
}

void HSVStrip::updateStripPixel(uint16_t n)
{
    uint32_t neoColor = Adafruit_NeoPixel::ColorHSV(strip_HSV[n].h, strip_HSV[n].s, strip_HSV[n].v);
    if(bUsesGammaCorrection)
    {
        neoColor = Adafruit_NeoPixel::gamma32(neoColor);
    }
    strip.setPixelColor(n, neoColor);
}

HSV HSVStrip::getPixel(uint16_t n)
{
    return strip_HSV[n];
}

void HSVStrip::show()
{
    strip.show();
}