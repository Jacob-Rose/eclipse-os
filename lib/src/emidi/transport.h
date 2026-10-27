// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstddef>
#include <cstdint>

#include "message.h"
#include "parser.h"

namespace emidi
{
    /* @brief Where MIDI bytes come from and go to: a UART at 31250 baud, a
    * USB MIDI endpoint, a byte queue in a test. Must not block.
    */
    class Transport
    {
    public:
        virtual ~Transport() = default;

        /// One byte, or -1 when nothing is waiting.
        virtual int readByte() = 0;

        /// Sends bytes out, for a relic that talks back. False if it cannot.
        virtual bool write(const uint8_t* data, size_t length) { (void)data; (void)length; return false; }
    };

    /* @brief Pumps a Transport through a Parser into a MessageSink, once a
    * tick. The relic's end of a MIDI cable:
    *
    *     port.setTransport(&uart);
    *     ...
    *     port.tick(nowSeconds, sink);   // every frame
    */
    class MidiPort
    {
    public:
        void setTransport(Transport* inTransport) { transport = inTransport; parser.reset(); }
        Transport* getTransport() const { return transport; }

        /// Drains whatever has arrived. `when` stamps every message from this tick.
        void tick(double when, MessageSink& sink)
        {
            if (transport == nullptr)
            {
                return;
            }
            int byte = 0;
            while ((byte = transport->readByte()) >= 0)
            {
                parser.feed(static_cast<uint8_t>(byte), when, sink);
                ++bytesRead;
            }
        }

        /// Sends one message, for a relic that answers.
        bool send(const Message& message)
        {
            if (transport == nullptr)
            {
                return false;
            }
            uint8_t bytes[3] = {message.status, message.data1, message.data2};
            size_t length = 3;
            if (message.isRealtime() || message.status == status::TuneRequest)
            {
                length = 1;
            }
            else
            {
                const uint8_t kind = message.kind();
                if (kind == status::ProgramChange || kind == status::ChannelAftertouch
                    || message.status == status::TimeCode || message.status == status::SongSelect)
                {
                    length = 2;
                }
            }
            return transport->write(bytes, length);
        }

        uint32_t getBytesRead() const { return bytesRead; }

    private:
        Transport* transport{nullptr};
        Parser parser;
        uint32_t bytesRead{0};
    };
}
