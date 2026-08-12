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
    /// The link over the relic's own USB cable.
    ///
    /// On an RP2040 `Serial` is USB CDC, not a UART, and the baud rate handed
    /// to Serial.begin is a number the host sets and neither end obeys —
    /// throughput is USB's, on the order of a megabyte a second. The obelisk's
    /// 1032-byte frames at 40fps are about 41 KB/s of that, so the wire has
    /// never been the constraint here. What is: 344 WS2812s take 10.3ms to
    /// clock out, against a 25ms budget.
    ///
    /// One byte at a time rather than readBytes(), because readBytes blocks
    /// until it has what you asked for or its timeout expires, and a render
    /// loop cannot afford either.
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

        /// Into the boot ROM, and that is the last thing this program does.
        ///
        /// The core also reboots on the classic 1200-baud touch, which is what
        /// `arduino-cli upload` uses and what keeps every other tool working.
        /// This exists beside it because the touch means closing the port and
        /// reopening it at a different rate, and a desk that already holds the
        /// port open for a show would rather just ask.
        void rebootToBootloader() override
        {
            Serial.flush();     // the "EOSLINK bootsel" line, before we go
            delay(20);          // and time for the host to actually read it
            reset_usb_boot(0, 0);
        }
    };
}

#endif
