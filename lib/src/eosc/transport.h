// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstddef>
#include <cstdint>

#include <enet/datagram.h>

#include "message.h"
#include "packet.h"

namespace eosc
{
    /* @brief Pumps a datagram transport: every waiting packet is decoded and
    * its messages handed to the sink, and a message can be sent back.
    *
    *     OscPort port;
    *     port.setTransport(&udp);              // an enet::ArduinoUdp or HostUdp
    *     port.tick(now, sink);                 // once a frame, or from a thread
    *     port.send(Message("/lamp").add(1.0f));
    */
    class OscPort
    {
    public:
        /// The biggest datagram taken. OSC over UDP stays well inside this.
        static constexpr size_t kDatagramCapacity = 4096;

        void setTransport(enet::DatagramTransport* inTransport) { transport = inTransport; }
        enet::DatagramTransport* getTransport() const { return transport; }

        /// Drains the transport. Returns how many messages were handed on.
        int tick(double when, MessageSink& sink)
        {
            int messages = 0;
            while (transport != nullptr)
            {
                const int length = transport->receive(buffer, kDatagramCapacity);
                if (length == 0) break;
                if (length < 0)
                {
                    ++oversized;
                    continue;
                }
                ++packets;
                const int decoded = Packet::decode(buffer, static_cast<size_t>(length), sink, when);
                if (decoded == 0) ++undecodable;
                messages += decoded;
            }
            return messages;
        }

        bool send(const Message& message)
        {
            if (transport == nullptr) return false;
            const size_t length = Packet::encode(message, buffer, kDatagramCapacity);
            if (length == 0 || !transport->send(buffer, length))
            {
                ++dropped;
                return false;
            }
            ++sent;
            return true;
        }

        uint32_t getPackets() const { return packets; }
        uint32_t getUndecodable() const { return undecodable; }
        uint32_t getOversized() const { return oversized; }
        uint32_t getSent() const { return sent; }
        uint32_t getDropped() const { return dropped; }

    private:
        enet::DatagramTransport* transport{nullptr};
        uint8_t buffer[kDatagramCapacity];
        uint32_t packets{0};
        uint32_t undecodable{0};
        uint32_t oversized{0};
        uint32_t sent{0};
        uint32_t dropped{0};
    };
}
