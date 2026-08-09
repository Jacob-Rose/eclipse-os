// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "screen_drawer.h"

// was #ifdef, which was always true: USE_SCREEN is always *defined*, it is its
// *value* that says whether there is a screen.
#if USE_SCREEN

#include "../ecore/logging.h"

using namespace eio;
using namespace ecore::log;

ScreenDrawer::ScreenDrawer()
{
#if USE_SCREEN
    img.begin(LITTLE_ENDIAN_PIXELS);
#endif
}

#if USE_SCREEN
void ScreenDrawer::setScreenRef(std::shared_ptr<Adafruit_GC9A01A> inScreenRef)
{
    ScreenRef = inScreenRef;
}
#endif

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
#if USE_SCREEN
    ScreenRef->startWrite();

    int playFrameResult = img.playFrame(true, NULL, this);
    img.getLastError();
    if(playFrameResult == -1)
    {
        dbgLog("ScreenDrawer::tick - playFrameResult == -1", Verbosity::Error, Category::OnTick | Category::Screen);
    }

    ScreenRef->endWrite();
#endif
}

void ScreenDrawer::setScreenGif(const uint8_t* data, int size)
{
    bWasCancelled = true;
    bImgReady = false;

    //TODO BAD CASTING
    // this is a hack to get around the fact that AnimatedGIF wants a uint8_t* and we have a const uint8_t*
    // this is a bad idea, but it works for now
    // we should probably create a new hash of data
    uint8_t* castData = const_cast<uint8_t*>(data);

    

#if USE_SCREEN
    img.open(castData, size, eio::ScreenDrawer::GIFDraw_UpscaleScreen);
    int lastError = img.getLastError();
    if(lastError != 0)
    {
        dbgLog("ScreenDrawer::setScreenGif - lastError == " + std::to_string(lastError), Verbosity::Error, Category::OnTick | Category::Screen);
    }

    bImgReady = true;
#endif
}

void ScreenDrawer::cancelGifRender()
{
    bWasCancelled = true;
}

#if USE_SCREEN
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

        SD->setPixelColor(xImagePixel, yImagePixel, c);
        Screen->writeFillRect(startingScreenPixelX, yStartingScreenPixel, xScreenToPixelSize, yScreenToPixelSize, c);
    }
}
#endif


#endif