// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#include "transport.h"

#if USE_ARDUINO && defined(USE_TINYUSB)

#include <Adafruit_TinyUSB.h>

namespace emidi
{
    /* @brief The relic as a class-compliant USB MIDI device: it shows up as a
    * MIDI port on whatever it is plugged into, beside the serial port elink
    * already uses. Needs the sketch built with the Adafruit TinyUSB stack
    * (arduino-pico's "USB Stack" option; it defines USE_TINYUSB). Untested on
    * a board as of writing.
    *
    *     static emidi::UsbMidiTransport usb;
    *     usb.begin();            // before Serial.begin on a TinyUSB build
    *     port.setTransport(&usb);
    */
    class UsbMidiTransport : public Transport
    {
    public:
        void begin()
        {
            device.begin();
        }

        int readByte() override
        {
            return device.available() ? device.read() : -1;
        }

        bool write(const uint8_t* data, size_t length) override
        {
            return device.write(data, length) == length;
        }

        /// True once the host has enumerated the device.
        bool isMounted() const { return TinyUSBDevice.mounted(); }

    private:
        Adafruit_USBD_MIDI device;
    };
}

#endif
