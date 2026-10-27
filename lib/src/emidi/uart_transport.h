// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#include "transport.h"

#if USE_ARDUINO

#include <Arduino.h>

namespace emidi
{
    /* @brief Where a relic's DIN MIDI jacks are wired: which UART and its
    * pins. Declared per relic in relics/wiring.h. An optoisolator on the
    * way in and a current loop on the way out, as the MIDI spec draws them.
    */
    struct MidiWiring
    {
        int uart{1};      ///< 0 or 1
        int rxPin{-1};    ///< -1 for no input
        int txPin{-1};    ///< -1 for no output
    };

    /* @brief MIDI over one of the RP2040's UARTs at 31250 baud, 8N1: the
    * five-pin DIN world. Untested on a board as of writing.
    *
    *     static emidi::UartMidiTransport din(wiring::kObeliskMidi);
    *     din.begin();
    *     port.setTransport(&din);
    */
    class UartMidiTransport : public Transport
    {
    public:
        explicit UartMidiTransport(const MidiWiring& inWiring)
            : wiring(inWiring), serial(inWiring.uart == 0 ? Serial1 : Serial2) {}

        void begin()
        {
            if (wiring.rxPin >= 0)
            {
                serial.setRX(static_cast<pin_size_t>(wiring.rxPin));
            }
            if (wiring.txPin >= 0)
            {
                serial.setTX(static_cast<pin_size_t>(wiring.txPin));
            }
            serial.begin(31250, SERIAL_8N1);
        }

        void end() { serial.end(); }

        int readByte() override
        {
            return serial.available() ? serial.read() : -1;
        }

        bool write(const uint8_t* data, size_t length) override
        {
            if (wiring.txPin < 0)
            {
                return false;
            }
            return serial.write(data, length) == length;
        }

    private:
        MidiWiring wiring;
        SerialUART& serial;
    };
}

#endif
