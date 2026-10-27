// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "message.h"

namespace eosc
{
    /* @brief The wire form: OSC 1.0, big-endian, four-byte aligned.
    *
    * decode() takes one datagram and hands every message in it to the sink,
    * unpacking bundles recursively (their timetags are ignored: everything
    * aimed at a light says "now"). Malformed input yields what could be read
    * and never throws, because anything on the network can send a packet.
    *
    *     Packet::decode(data, length, sink, when);
    *
    *     uint8_t out[256];
    *     const size_t n = Packet::encode(message, out, sizeof(out));   // 0 if it did not fit
    */
    namespace Packet
    {
        /// Messages handed to the sink.
        int decode(const uint8_t* data, size_t length, MessageSink& sink, double when);

        /// One message, no bundle. False if `data` is not one.
        bool decodeOne(const uint8_t* data, size_t length, Message& outMessage);

        /// Bytes written, or 0 when the message does not fit in `capacity`.
        size_t encode(const Message& message, uint8_t* out, size_t capacity);

        /// The same, into a vector.
        std::vector<uint8_t> encode(const Message& message);

        /// Bytes a message needs on the wire.
        size_t encodedSize(const Message& message);
    }
}
