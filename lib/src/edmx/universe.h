// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

///
/// edmx: DMX for the library. A channel buffer, the fixtures patched into
/// it, and the interface an output implements to put it on a wire. The
/// transports - a USB widget on a desk, a UART on a relic - live with their
/// platform; everything here builds on both.
///

namespace edmx
{
    /// Channels in one DMX512 universe.
    static constexpr int DMX_CHANNEL_COUNT = 512;

    /* @brief A run of channel values addressed 1..N, the way fixture manuals
    * number them. Any size: a DMX universe is 512, a pixel rig driven over a
    * relic link is three per pixel.
    */
    class ChannelBuffer
    {
    public:
        ChannelBuffer() : ChannelBuffer(DMX_CHANNEL_COUNT) {}

        explicit ChannelBuffer(int inChannelCount)
        {
            resize(inChannelCount);
        }

        /// At least one channel.
        void resize(int inChannelCount)
        {
            channels.assign(static_cast<size_t>(std::max(inChannelCount, 1)), 0);
        }

        void clear() { std::fill(channels.begin(), channels.end(), uint8_t{0}); }

        /// Out-of-range channels are ignored.
        void setChannel(int channel1Based, uint8_t value)
        {
            if (channel1Based < 1 || static_cast<size_t>(channel1Based) > channels.size())
            {
                return;
            }
            channels[static_cast<size_t>(channel1Based - 1)] = value;
        }

        /// Out-of-range channels read 0.
        uint8_t getChannel(int channel1Based) const
        {
            if (channel1Based < 1 || static_cast<size_t>(channel1Based) > channels.size())
            {
                return 0;
            }
            return channels[static_cast<size_t>(channel1Based - 1)];
        }

        const uint8_t* data() const { return channels.data(); }
        size_t size() const { return channels.size(); }

    private:
        std::vector<uint8_t> channels;
    };


    /* @brief One DMX512 universe: a ChannelBuffer of exactly 512 channels. */
    class DmxUniverse : public ChannelBuffer
    {
    public:
        DmxUniverse() : ChannelBuffer(DMX_CHANNEL_COUNT) {}
    };
}
