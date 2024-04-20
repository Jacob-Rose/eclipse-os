// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jio.h"

#include "jlogging.h"

using namespace j;

void Button::init(uint8_t inPin)
{
    pinMode(inPin, INPUT_PULLUP);
    myPin = inPin;
}

bool Button::pollPressed()
{
    return !digitalRead(myPin);
}

void Button::tick(float deltaTime)
{
    timeSinceStateChanged += deltaTime;
    if(pollPressed())
    {
        if(!bLastSeenPressed)
        {
            bLastSeenPressed = true;
            timeSinceStateChanged = 0.0f;
        }
    }
    else
    {
        if(bLastSeenPressed)
        {
            bLastSeenPressed = false;
            timeSinceStateChanged = 0.0f;
        }
    }
}

void Button::resetTimeSinceStateChange()
{
    timeSinceStateChanged = 0.0f;
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
        colors.resize(count, 100);
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

void ScreenDrawer::renderGif(AnimatedGIF& gif)
{
    bWasCancelled = false;
    ScreenRef->startWrite();
    int playFrameResult = gif.playFrame(true, NULL, this);
    ScreenRef->endWrite();
}

void ScreenDrawer::cancelGifRender()
{
    bWasCancelled = true;
}

/*static*/ void ScreenDrawer::GIFDraw_UpscaleScreen(GIFDRAW* pDraw)
{
    ScreenDrawer* SD = static_cast<ScreenDrawer*>(pDraw->pUser);

    Adafruit_GC9A01A* Screen = SD->ScreenRef.get();
    if(Screen == nullptr)
    {
        return;
    }

    if(SD->bWasCancelled)
    {
        return;
    }

    uint8_t *s = pDraw->pPixels;
    uint16_t *d, *usPalette, usTemp[320];

    const int yImagePixel = pDraw->y;

    usPalette = pDraw->pPalette;

    float ScalarFloatY = ((float)Screen->height()) / pDraw->iHeight;

    int startingPixelY = (yImagePixel * ScalarFloatY);
    int endingPixelY = ((yImagePixel + 1) * ScalarFloatY);
    int yScreenToPixelSize = endingPixelY - startingPixelY;

    int16_t yStartingScreenPixel = pDraw->iY + (startingPixelY); // current line

    if(yImagePixel == 0)
    {
        SD->setCanvasSize(pDraw->iWidth, pDraw->iHeight);
    }


    for(int16_t xImagePixel = 0; xImagePixel < pDraw->iWidth; ++xImagePixel)
    {
        uint16_t c = pDraw->pPalette[pDraw->pPixels[xImagePixel]];
        if(c == SD->getPixelColor(xImagePixel, yImagePixel))
        {
            continue;
        }
        float ScalarFloatX = ((float)Screen->width()) / pDraw->iWidth;

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

void HSVStrip::setHSV(uint16_t idx, float h, uint8_t s, uint8_t v)
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
    uint32_t neoColor = Adafruit_NeoPixel::ColorHSV(strip_HSV[idx].getHueAs16(), strip_HSV[idx].s, strip_HSV[idx].v);
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

uint8_t HSVStrip::getStripBrightness() const
{
    return strip.getBrightness();
}

void HSVStrip::setStripBrightness(uint8_t inBrightness)
{
    strip.setBrightness(inBrightness);
}