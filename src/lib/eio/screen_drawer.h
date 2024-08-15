// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include <AnimatedGIF.h>

#include <Adafruit_GC9A01A.h>

#include <memory>
#include <chrono>
#include <ctime>

#include "../ecore/tickable.h"

using namespace ecore;

namespace eio
{
    // screen drawer for drawing pixel art
    // has performance solutions that optimize for pixel art on multiple stages
    class ScreenDrawer : public Tickable
    {
    public:
        ScreenDrawer();

        // Tickable interface
        virtual void tick(float deltaTime) override;

        void setCanvasSize(uint16_t x, uint16_t y);
        void setScreenRef(std::shared_ptr<Adafruit_GC9A01A> inScreenRef);

        // using canvas pixels, lets us scale our performance with our image size
        uint16_t getPixelColor(uint16_t x, uint16_t y);
        void setPixelColor(uint16_t x, uint16_t y, uint16_t color);

        static void GIFDraw_UpscaleScreen(GIFDRAW *pDraw);

        void setScreenGif(uint8_t* data, int size);
        void cancelGifRender();

    public:
        std::shared_ptr<Adafruit_GC9A01A> ScreenRef;

    private:
        bool bCanvasEnabled = false;
        int16_t xCanvasSize, yCanvasSize;
        std::vector<uint16_t> colors;

        AnimatedGIF img;
        bool bImgReady = false;

    private:
        bool bWasCancelled = false;
    };





};