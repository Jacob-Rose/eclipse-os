// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>

#include "universe.h"

namespace edmx
{
    /* @brief Something that takes a channel buffer and puts it on a wire.
    *
    * Implement one per transport: a USB widget on a desk, a UART on a relic,
    * a console for debugging. A DMX transport sends the first
    * DMX_CHANNEL_COUNT channels at most; setUniverseLength() trims that to
    * what the rig uses.
    *
    *     if (!output.open(error)) ...
    *     output.sendFrame(buffer, error);   // once a frame
    *     output.close();
    */
    class DmxOutput
    {
    public:
        virtual ~DmxOutput() = default;

        virtual bool open(std::string& outError) = 0;
        virtual void close() = 0;
        virtual bool isOpen() const = 0;

        /// Pushes one frame. False, with outError, is fatal to the show.
        virtual bool sendFrame(const ChannelBuffer& buffer, std::string& outError) = 0;

        /// Send only the first `channels` slots. A receiver keeps what it is
        /// not sent, so a short frame is a faster frame; transports clamp
        /// this to what their hardware accepts.
        virtual void setUniverseLength(int channels) { (void)channels; }

        /// Frames per second this output can sustain, or 0 for no limit.
        /// Render at this rate rather than queueing frames a wire cannot carry.
        virtual float maxFrameRate() const { return 0.0f; }

        virtual std::string describe() const = 0;
    };
}
