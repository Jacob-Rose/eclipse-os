// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../ecore/core.h"

#include "relic_link.h"

#if USE_ARDUINO

#include <pico/bootrom.h>

namespace elink
{
    /* @brief The link over the relic's USB cable (Serial is USB CDC on an
    * RP2040; the baud rate is nominal). Reads one byte at a time so the
    * render loop never blocks.
    */
    class SerialLinkTransport : public LinkTransport
    {
    public:
        int readByte() override
        {
            return Serial.available() ? Serial.read() : -1;
        }

        void writeLine(const char* text) override
        {
            Serial.println(text);
        }

        /// Into the boot ROM; the last thing this program does. The classic
        /// 1200-baud touch still works too; this lets a desk that holds the
        /// port open ask without reopening it.
        void rebootToBootloader() override
        {
            Serial.flush();     // the "EOSLINK bootsel" line, before we go
            delay(20);          // and time for the host to actually read it
            reset_usb_boot(0, 0);
        }
    };
}

#endif
