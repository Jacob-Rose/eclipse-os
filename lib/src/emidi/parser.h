// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstddef>
#include <cstdint>

#include "message.h"

namespace emidi
{
    /* @brief Turns a raw MIDI byte stream into Messages.
    *
    * Handles running status, realtime bytes arriving inside another message,
    * and the one- and two-byte system common messages. Sysex is skipped.
    * Feed it bytes as they arrive, in any chunking:
    *
    *     parser.feed(bytes, length, when, sink);
    */
    class Parser
    {
    public:
        void feed(uint8_t byte, double when, MessageSink& sink);
        void feed(const uint8_t* data, size_t length, double when, MessageSink& sink);

        /// Forgets any partial message and the running status.
        void reset();

    private:
        uint8_t runningStatus{0};
        uint8_t pending[2]{0, 0};   // status, first data byte
        int collected{0};
        int wanted{0};
    };
}
