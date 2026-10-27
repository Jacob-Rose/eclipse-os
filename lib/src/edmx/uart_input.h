// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#include "universe.h"

#if USE_ARDUINO

#include <hardware/gpio.h>
#include <hardware/uart.h>

namespace edmx
{
    /* @brief Where a relic listens for DMX: which UART and its RX pin.
    * Declared per relic in relics/wiring.h.
    */
    struct DmxInputWiring
    {
        int uart{1};      ///< 0 or 1
        int rxPin{0};
    };

    /* @brief DMX512 in, off one of the RP2040's UARTs through an RS-485
    * transceiver. The break that starts a frame arrives as a framing error,
    * which is how a frame boundary is found; the slots after it fill the
    * buffer. Pump it every tick; bind channels to a look's properties with
    * ChannelBinding. Untested on a board as of writing.
    *
    *     dmxIn.open(error);
    *     ...
    *     if (dmxIn.tick()) binding.apply(dmxIn.channels(), look);
    */
    class UartDmxInput
    {
    public:
        explicit UartDmxInput(const DmxInputWiring& inWiring) : wiring(inWiring) {}

        bool open(std::string& outError)
        {
            if (wiring.uart != 0 && wiring.uart != 1)
            {
                outError = "dmx in: uart must be 0 or 1";
                return false;
            }
            uart = (wiring.uart == 0) ? uart0 : uart1;
            uart_init(uart, 250000);
            uart_set_format(uart, 8, 2, UART_PARITY_NONE);
            uart_set_fifo_enabled(uart, true);
            gpio_set_function(static_cast<uint>(wiring.rxPin), GPIO_FUNC_UART);
            opened = true;
            return true;
        }

        void close()
        {
            if (opened)
            {
                uart_deinit(uart);
                opened = false;
            }
        }

        bool isOpen() const { return opened; }

        /// Drains the UART. True when a frame completed since the last call,
        /// so channels() holds something new.
        bool tick()
        {
            bool completed = false;
            while (opened && uart_is_readable(uart))
            {
                // the raw data register carries the error bits; a break
                // error is the frame boundary, not a bad byte
                const uint32_t word = uart_get_hw(uart)->dr;
                if ((word & UART_UARTDR_BE_BITS) != 0)
                {
                    if (slot > 1)
                    {
                        ++frames;
                        completed = true;
                    }
                    slot = 0;
                    continue;
                }
                if ((word & (UART_UARTDR_FE_BITS | UART_UARTDR_PE_BITS)) != 0)
                {
                    continue;
                }

                const uint8_t byte = static_cast<uint8_t>(word & 0xFF);
                if (slot == 0)
                {
                    // the start code: only the null start code is a lighting frame
                    inFrame = (byte == 0);
                }
                else if (inFrame && slot <= DMX_CHANNEL_COUNT)
                {
                    buffer.setChannel(slot, byte);
                }
                ++slot;
            }
            return completed;
        }

        const ChannelBuffer& channels() const { return buffer; }
        uint32_t framesReceived() const { return frames; }

    private:
        DmxInputWiring wiring;
        uart_inst_t* uart{nullptr};
        bool opened{false};
        bool inFrame{false};
        int slot{0};
        uint32_t frames{0};
        ChannelBuffer buffer{DMX_CHANNEL_COUNT};
    };
}

#endif
