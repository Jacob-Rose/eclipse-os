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

bool Button::isPressed()
{
    return !digitalRead(myPin);
}

ScreenDrawer::ScreenDrawer()
{

}

void ScreenDrawer::setScreenRef(std::shared_ptr<Adafruit_GC9A01A> inScreenRef)
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

    Adafruit_GC9A01A* Screen = SD->ScreenRef.get();
    if(Screen == nullptr)
    {
        return;
    }

    uint8_t *s = pDraw->pPixels;
    uint16_t *d, *usPalette, usTemp[320];

    usPalette = pDraw->pPalette;

    float ScalarFloatY = ((float)SCREEN_HEIGHT) / pDraw->iHeight;

    int startingPixelY = (pDraw->y * ScalarFloatY);
    int endingPixelY = ((pDraw->y + 1) * ScalarFloatY);
    int ySize = endingPixelY - startingPixelY;

    int16_t y = pDraw->iY + (startingPixelY); // current line

    if (y >= SCREEN_HEIGHT || pDraw->iX >= SCREEN_HEIGHT || pDraw->iWidth < 1)
        return;

    if(y == 0)
    {
        SD->setCanvasSize(pDraw->iWidth, pDraw->iHeight);
    }

    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
        for (uint16_t x=0; x < pDraw->iWidth; x++)
        {
            if (s[x] == pDraw->ucTransparent)
            s[x] = pDraw->ucBackground;
        }
        pDraw->ucHasTransparency = 0;
    }

    for(int16_t xPixel = 0; xPixel < pDraw->iWidth; ++xPixel)
    {
        uint16_t c = pDraw->pPalette[pDraw->pPixels[xPixel]];
        float ScalarFloatX = ((float)SCREEN_WIDTH) / pDraw->iWidth;

        int startingPixelX = (xPixel * ScalarFloatX);
        int endingPixelX = ((xPixel + 1) * ScalarFloatX);
        int xSize = endingPixelX - startingPixelX;

        /*
        Serial.print("spx: ");
        Serial.print(startingPixelX);
        Serial.print(" epx: ");
        Serial.print(endingPixelX);
        Serial.print(" sz: ");
        Serial.print(xSize);
        Serial.print(" c: ");
        Serial.print(c);
        */
        Screen->writeFillRect(startingPixelX, y, xSize, ySize, c);
    }
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

HSV HSVStrip::getHSV(uint16_t idx)
{
    return strip_HSV[idx];
}

void HSVStrip::setHSV(uint16_t idx, const HSV& hsv)
{
    strip_HSV[idx].h = hsv.h;
    strip_HSV[idx].s = hsv.s;
    strip_HSV[idx].v = hsv.v;

    updateStripPixel(idx);
}

void HSVStrip::setHSV(uint16_t idx, uint16_t h, uint8_t s, uint8_t v)
{
    strip_HSV[idx].h = h;
    strip_HSV[idx].s = s;
    strip_HSV[idx].v = v;

    updateStripPixel(idx);
}

__UINT8_TYPE__ HSVStrip::getBrightness(uint16_t idx)
{
    return strip_HSV[idx].v;
}

void HSVStrip::setBrightness(uint16_t idx, uint8_t val)
{
    strip_HSV[idx].v = val;

    updateStripPixel(idx);
}

void HSVStrip::updateStripPixel(uint16_t idx)
{
    uint32_t neoColor = Adafruit_NeoPixel::ColorHSV(strip_HSV[idx].h, strip_HSV[idx].s, strip_HSV[idx].v);
    if(bUsesGammaCorrection)
    {
        neoColor = Adafruit_NeoPixel::gamma32(neoColor);
    }
    strip.setPixelColor(idx, neoColor);
}

void HSVStrip::show()
{
    strip.show();
}