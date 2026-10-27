// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#include "datagram.h"

#if !USE_ARDUINO

#include <string>

namespace enet
{
    /* @brief A UDP socket on a desk: BSD sockets, or winsock on Windows, the
    * same way up. Listens, or points at a remote and sends, or both.
    *
    * receive() waits up to the timeout for a datagram, so a thread can park
    * on it; set it to 0 to poll.
    *
    *     HostUdp in;
    *     in.listen("0.0.0.0", 7000, error);
    *     HostUdp out;
    *     out.point("mac-mini.local", 6000, error);   // a name is looked up here
    */
    class HostUdp : public DatagramTransport
    {
    public:
        HostUdp() = default;
        ~HostUdp() override;

        HostUdp(const HostUdp&) = delete;
        HostUdp& operator=(const HostUdp&) = delete;

        /// Binds to an address and port; port 0 takes any free one.
        bool listen(const std::string& host, int port, std::string& outError);

        /// Resolves a host and remembers it as where send() goes. Opens a
        /// socket if there is none yet.
        bool point(const std::string& host, int port, std::string& outError);

        void close();
        bool isOpen() const { return handle >= 0; }

        /// How long receive() waits, in milliseconds. 250 by default.
        void setTimeout(int milliseconds);

        int receive(uint8_t* buffer, size_t capacity) override;
        bool send(const uint8_t* data, size_t length) override;

        /// The port actually bound, after listen().
        int getLocalPort() const { return localPort; }
        /// The remote's numeric address after point(), as text.
        const std::string& getRemoteAddress() const { return remoteText; }

        /// Looks a host up without opening anything. False with the reason.
        static bool resolve(const std::string& host, int port, std::string& outAddress, std::string& outError);

        /// Whether a host is a numeric address rather than a name.
        static bool isLiteral(const std::string& host);

    private:
        bool open(int family, std::string& outError);

        long long handle{-1};
        int family{0};
        int localPort{0};
        int timeoutMs{250};
        unsigned char remote[128]{};   ///< a sockaddr_storage, kept opaque here
        int remoteLength{0};
        std::string remoteText;
    };
}

#endif
