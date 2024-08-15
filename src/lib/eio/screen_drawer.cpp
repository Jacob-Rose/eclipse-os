// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "screen_drawer.h"

#include "../ecore/logging.h"

using namespace eio;

ScreenDrawer::ScreenDrawer()
{
    img.begin(LITTLE_ENDIAN_PIXELS);
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

void ScreenDrawer::tick(float deltaTime)
{
    if(!bImgReady)
    {
        return;
    }

    ScreenRef->startWrite();
    int playFrameResult = img.playFrame(true, NULL, this);
    ScreenRef->endWrite();
}

void ScreenDrawer::setScreenGif(uint8_t* data, int size)
{
    bWasCancelled = true;
    bImgReady = false;

    img.open(data, size, eio::ScreenDrawer::GIFDraw_UpscaleScreen);

    bImgReady = true;
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
        if(pDraw->y == pDraw->iHeight - 1)
        {
            SD->bWasCancelled = false;
        }
        return;
    }

    if(!SD->bImgReady)
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



