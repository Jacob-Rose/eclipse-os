// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jio.h"
#include "gm.h"

#include "jlogging.h"

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
        colors.resize(0);
        colors.resize(count, 0);
    }
    xCanvasSize = x;
    yCanvasSize = y;

    bCanvasEnabled = true;
}

void ScreenDrawer::setPixelColor(uint16_t x, uint16_t y, uint16_t color)
{
    colors[x*xCanvasSize + y] = color;
}

uint16_t ScreenDrawer::getPixelColor(uint16_t x, uint16_t y)
{
    return colors[x*xCanvasSize + y];
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

    const int yImagePixel = pDraw->y;

    usPalette = pDraw->pPalette;

    float ScalarFloatY = ((float)SCREEN_HEIGHT) / pDraw->iHeight;

    int startingPixelY = (yImagePixel * ScalarFloatY);
    int endingPixelY = ((yImagePixel + 1) * ScalarFloatY);
    int yScreenToPixelSize = endingPixelY - startingPixelY;

    int16_t yStartingScreenPixel = pDraw->iY + (startingPixelY); // current line

    if(yImagePixel == 0)
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

    for(int16_t xImagePixel = 0; xImagePixel < pDraw->iWidth; ++xImagePixel)
    {
        uint16_t c = pDraw->pPalette[pDraw->pPixels[xImagePixel]];
        if(c == SD->getPixelColor(xImagePixel, yImagePixel))
        {
            continue;
        }
        float ScalarFloatX = ((float)SCREEN_WIDTH) / pDraw->iWidth;

        int startingScreenPixelX = (xImagePixel * ScalarFloatX);
        int endingScreenPixelX = ((xImagePixel + 1) * ScalarFloatX);
        int xScreenToPixelSize = endingScreenPixelX - startingScreenPixelX;

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

        SD->setPixelColor(xImagePixel, yImagePixel, c);
        Screen->writeFillRect(startingScreenPixelX, yStartingScreenPixel, xScreenToPixelSize, yScreenToPixelSize, c);
    }
}



HSVStrip::HSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType) : strip(inLedCount, inLedPin, inPixelType)
{
    strip_HSV = std::vector<HSV>(inLedCount);

    strip.begin();
}

HSVStrip::~HSVStrip()
{
}

HSV HSVStrip::getHSV(uint16_t idx) const
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

__UINT8_TYPE__ HSVStrip::getBrightness(uint16_t idx) const
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

uint16_t HSVStrip::getLength() const
{
    return strip.numPixels();
}

/*
MappedHSVStrip::MappedHSVStrip(uint16_t inLedCount, uint16_t inLedPin, neoPixelType inPixelType) : HSVStrip(inLedCount, inLedPin, inPixelType)
{

}

MappedHSVStrip::~MappedHSVStrip() : ~~HSVStrip()
{

}

Coordinate MappedHSVStrip::getCoord(uint16_t idx)
{
    return coordinates[idx];
}
*/