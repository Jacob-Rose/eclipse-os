// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#include "datagram.h"

#if USE_ARDUINO

#include <Udp.h>

namespace enet
{
    /* @brief Any Arduino UDP socket as a transport: a WiFiUDP on a Pico W, an
    * EthernetUDP on a wired board, the same code. Listens on a port, sends to
    * one remote when told where. Untested on a board as of writing.
    *
    *     WiFiUDP socket;
    *     static enet::ArduinoUdp udp(socket);
    *     udp.begin(7000);                               // after the network is up
    *     udp.setRemote(IPAddress(192, 168, 1, 20), 6000);
    *     port.setTransport(&udp);
    */
    class ArduinoUdp : public DatagramTransport
    {
    public:
        explicit ArduinoUdp(UDP& inSocket) : socket(inSocket) {}

        bool begin(uint16_t port)
        {
            listening = socket.begin(port) == 1;
            return listening;
        }

        void end()
        {
            socket.stop();
            listening = false;
        }

        void setRemote(const IPAddress& inAddress, uint16_t inPort)
        {
            remoteAddress = inAddress;
            remotePort = inPort;
        }

        int receive(uint8_t* buffer, size_t capacity) override
        {
            if (!listening) return 0;
            const int size = socket.parsePacket();
            if (size <= 0) return 0;
            if (static_cast<size_t>(size) > capacity)
            {
                socket.flush();
                return -1;
            }
            return socket.read(buffer, static_cast<size_t>(size));
        }

        bool send(const uint8_t* data, size_t length) override
        {
            if (remotePort == 0) return false;
            if (socket.beginPacket(remoteAddress, remotePort) != 1) return false;
            socket.write(data, length);
            return socket.endPacket() == 1;
        }

        bool isListening() const { return listening; }

    private:
        UDP& socket;
        IPAddress remoteAddress;
        uint16_t remotePort{0};
        bool listening{false};
    };
}

#endif
