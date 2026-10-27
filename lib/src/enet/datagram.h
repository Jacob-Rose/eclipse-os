// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstddef>
#include <cstdint>

///
/// enet: the network as the library sees it. A datagram transport that a
/// protocol module (eosc) pumps, and the sockets behind it: any Arduino UDP
/// (WiFi on a Pico W, Ethernet on a wired one) and a BSD socket on a host.
/// Joining a network is a different job and stays with ewifi.
///

namespace enet
{
    /* @brief A datagram socket: whole packets in, whole packets out.
    *
    * @see ArduinoUdp for a WiFiUDP or an EthernetUDP
    * @see HostUdp for a desk
    */
    class DatagramTransport
    {
    public:
        virtual ~DatagramTransport() = default;

        /// One datagram into `buffer`, or 0 when none is waiting. Negative
        /// when a datagram was too big for the buffer and was dropped.
        virtual int receive(uint8_t* buffer, size_t capacity) = 0;

        /// One datagram out, to wherever this transport points. False when
        /// it does not send.
        virtual bool send(const uint8_t* data, size_t length) { (void)data; (void)length; return false; }
    };
}
