// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../ecore/core.h"

#define USE_SCREEN 1

#if USE_SCREEN
#include <AnimatedGIF.h>

#include <Adafruit_GC9A01A.h>
#endif

#include <memory>
#include <chrono>
#include <ctime>

#include "../ecore/tickable.h"

using namespace ecore;

namespace eio
{

    const int ScreenWidth = 240;
    const int ScreenHeight = 240;
    // screen drawer for drawing pixel art
    // has performance solutions that optimize for pixel art on multiple stages
    class ScreenDrawer : public Tickable
    {
    public:
        ScreenDrawer();

        // Tickable interface
        virtual void tick(float deltaTime) override;

        void setCanvasSize(uint16_t x, uint16_t y);
#if USE_SCREEN
        void setScreenRef(std::shared_ptr<Adafruit_GC9A01A> inScreenRef);
        static void GIFDraw_UpscaleScreen(GIFDRAW *pDraw);
#endif

        // using canvas pixels, lets us scale our performance with our image size
        uint16_t getPixelColor(uint16_t x, uint16_t y);
        void setPixelColor(uint16_t x, uint16_t y, uint16_t color);

        void setScreenGif(const uint8_t* data, int size);
        void cancelGifRender();

    private:
        bool bCanvasEnabled = false;
        int16_t xCanvasSize, yCanvasSize;
        std::vector<uint16_t> colors;

#if USE_SCREEN
        std::shared_ptr<Adafruit_GC9A01A> ScreenRef;
        AnimatedGIF img;
#endif
        bool bImgReady = false;

    private:
        bool bWasCancelled = false;
    };
}