// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#include "output.h"

#if USE_ARDUINO

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <pico/time.h>

namespace edmx
{
    /* @brief Where a relic's DMX port is wired: which UART, its TX pin, and the
    * transceiver's driver-enable pin (-1 if the transceiver is wired to
    * transmit always). Declared per relic in relics/wiring.h, like a strip.
    */
    struct DmxWiring
    {
        int uart{0};      ///< 0 or 1
        int txPin{0};
        int enablePin{-1};
    };

    /* @brief DMX512 out of one of the RP2040's UARTs, through an RS-485
    * transceiver. 250 kbaud 8N2, a break and mark before every frame, the
    * frame itself by DMA so the render loop never waits on the wire.
    *
    * A frame is sent when the previous one has left the wire; one asked for
    * while the last is still going is dropped, which at 40 frames a second
    * is the frame nobody saw. Untested on a board as of writing: the UART,
    * DMA and timing calls are the Pico SDK's, which arduino-pico exposes.
    *
    *     static edmx::UartDmxOutput dmx(wiring::kObeliskDmx);
    *     dmx.open(error);
    *     ...
    *     dmx.sendFrame(universe, error);   // once a frame
    */
    class UartDmxOutput : public DmxOutput
    {
    public:
        explicit UartDmxOutput(const DmxWiring& inWiring) : wiring(inWiring) {}

        bool open(std::string& outError) override
        {
            if (wiring.uart != 0 && wiring.uart != 1)
            {
                outError = "dmx: uart must be 0 or 1";
                return false;
            }
            uart = (wiring.uart == 0) ? uart0 : uart1;

            uart_init(uart, 250000);
            uart_set_format(uart, 8, 2, UART_PARITY_NONE);
            uart_set_fifo_enabled(uart, true);
            gpio_set_function(static_cast<uint>(wiring.txPin), GPIO_FUNC_UART);

            if (wiring.enablePin >= 0)
            {
                gpio_init(static_cast<uint>(wiring.enablePin));
                gpio_set_dir(static_cast<uint>(wiring.enablePin), GPIO_OUT);
                gpio_put(static_cast<uint>(wiring.enablePin), 1); // drive the bus
            }

            channel = dma_claim_unused_channel(false);
            if (channel < 0)
            {
                outError = "dmx: no free DMA channel";
                uart_deinit(uart);
                return false;
            }

            dma_channel_config config = dma_channel_get_default_config(static_cast<uint>(channel));
            channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
            channel_config_set_read_increment(&config, true);
            channel_config_set_write_increment(&config, false);
            channel_config_set_dreq(&config, uart_get_dreq(uart, true));
            dma_channel_configure(static_cast<uint>(channel), &config,
                                  &uart_get_hw(uart)->dr, frame, 0, false);

            opened = true;
            return true;
        }

        void close() override
        {
            if (!opened)
            {
                return;
            }
            dma_channel_wait_for_finish_blocking(static_cast<uint>(channel));
            dma_channel_unclaim(static_cast<uint>(channel));
            uart_deinit(uart);
            if (wiring.enablePin >= 0)
            {
                gpio_put(static_cast<uint>(wiring.enablePin), 0);
            }
            opened = false;
        }

        bool isOpen() const override { return opened; }

        bool sendFrame(const ChannelBuffer& buffer, std::string& outError) override
        {
            (void)outError;
            if (!opened)
            {
                return false;
            }

            // the last frame is still on the wire: this one is the dropped one
            if (dma_channel_is_busy(static_cast<uint>(channel))
                || (uart_get_hw(uart)->fr & UART_UARTFR_BUSY_BITS) != 0)
            {
                ++dropped;
                return true;
            }

            // start code, then the channels; the rest of a short buffer is zero
            frame[0] = 0;
            const size_t carried = std::min(buffer.size(), static_cast<size_t>(universeLength));
            std::copy(buffer.data(), buffer.data() + carried, frame + 1);
            std::fill(frame + 1 + carried, frame + 1 + universeLength, uint8_t{0});

            // break (>= 88us) and mark after break (>= 8us), by hand: they are
            // the one part of a DMX frame a UART cannot say on its own
            uart_set_break(uart, true);
            busy_wait_us(kBreakMicroseconds);
            uart_set_break(uart, false);
            busy_wait_us(kMarkMicroseconds);

            dma_channel_set_read_addr(static_cast<uint>(channel), frame, false);
            dma_channel_set_trans_count(static_cast<uint>(channel), 1 + static_cast<uint>(universeLength), true);
            ++sent;
            return true;
        }

        /// 24..512; a receiver keeps what it is not sent.
        void setUniverseLength(int channels) override
        {
            universeLength = std::max(24, std::min(channels, DMX_CHANNEL_COUNT));
        }

        /// 44 is a full universe back to back; the frame itself is 23ms.
        float maxFrameRate() const override { return 40.0f; }

        std::string describe() const override
        {
            return "dmx uart" + std::to_string(wiring.uart) + " tx" + std::to_string(wiring.txPin);
        }

        uint32_t framesSent() const { return sent; }
        uint32_t framesDropped() const { return dropped; }

    private:
        static constexpr uint32_t kBreakMicroseconds = 100;
        static constexpr uint32_t kMarkMicroseconds = 12;

        DmxWiring wiring;
        uart_inst_t* uart{nullptr};
        int channel{-1};
        bool opened{false};
        int universeLength{DMX_CHANNEL_COUNT};

        /// start code plus a universe, read by the DMA while it goes out
        uint8_t frame[1 + DMX_CHANNEL_COUNT]{};
        uint32_t sent{0};
        uint32_t dropped{0};
    };
}

#endif
