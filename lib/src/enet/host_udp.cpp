// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "host_udp.h"

#if !USE_ARDUINO

#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

using namespace enet;

namespace
{
#if defined(_WIN32)
    using SocketHandle = SOCKET;
    constexpr SocketHandle kNoSocket = INVALID_SOCKET;

    void startup()
    {
        static bool started = false;
        if (!started)
        {
            WSADATA data;
            WSAStartup(MAKEWORD(2, 2), &data);
            started = true;
        }
    }

    std::string lastError()
    {
        return "winsock error " + std::to_string(WSAGetLastError());
    }

    void closeSocket(SocketHandle s) { closesocket(s); }
#else
    using SocketHandle = int;
    constexpr SocketHandle kNoSocket = -1;

    void startup() {}

    std::string lastError()
    {
        return std::string(std::strerror(errno));
    }

    void closeSocket(SocketHandle s) { ::close(s); }
#endif

    bool lookup(const std::string& host, int port, int socktype, addrinfo** outInfo, std::string& outError)
    {
        startup();
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = socktype;
        hints.ai_flags = AI_ADDRCONFIG;
        const std::string service = std::to_string(port);
        const int result = getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints, outInfo);
        if (result != 0)
        {
#if defined(_WIN32)
            outError = "cannot resolve '" + host + "': " + lastError();
#else
            outError = "cannot resolve '" + host + "': " + gai_strerror(result);
#endif
            return false;
        }
        return true;
    }

    /// The answer to use: one in the family wanted if there is one, else an
    /// IPv4 one ("localhost" answers ::1 first on some machines, and the
    /// other end is usually a v4 listener), else the first.
    const addrinfo* pick(const addrinfo* info, int wantedFamily)
    {
        for (const addrinfo* each = info; each != nullptr; each = each->ai_next)
        {
            if (wantedFamily != 0 && each->ai_family == wantedFamily) return each;
        }
        for (const addrinfo* each = info; each != nullptr; each = each->ai_next)
        {
            if (each->ai_family == AF_INET) return each;
        }
        return info;
    }

    std::string addressText(const sockaddr* address, int length)
    {
        char host[NI_MAXHOST] = {0};
        char service[NI_MAXSERV] = {0};
        if (getnameinfo(address, static_cast<socklen_t>(length), host, sizeof(host), service, sizeof(service),
                        NI_NUMERICHOST | NI_NUMERICSERV) != 0)
        {
            return "?";
        }
        std::string text(host);
        if (text.find(':') != std::string::npos) text = "[" + text + "]";
        return text + ":" + service;
    }
}

HostUdp::~HostUdp()
{
    close();
}

bool HostUdp::isLiteral(const std::string& host)
{
    in_addr v4{};
    in6_addr v6{};
    startup();
    return inet_pton(AF_INET, host.c_str(), &v4) == 1 || inet_pton(AF_INET6, host.c_str(), &v6) == 1;
}

bool HostUdp::resolve(const std::string& host, int port, std::string& outAddress, std::string& outError)
{
    addrinfo* info = nullptr;
    if (!lookup(host, port, SOCK_DGRAM, &info, outError))
    {
        return false;
    }
    const addrinfo* chosen = pick(info, 0);
    outAddress = addressText(chosen->ai_addr, static_cast<int>(chosen->ai_addrlen));
    freeaddrinfo(info);
    return true;
}

bool HostUdp::open(int inFamily, std::string& outError)
{
    if (handle >= 0 && family == inFamily)
    {
        return true;
    }
    close();
    startup();
    const SocketHandle s = ::socket(inFamily, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNoSocket)
    {
        outError = "cannot open a socket: " + lastError();
        return false;
    }
    handle = static_cast<long long>(s);
    family = inFamily;
    setTimeout(timeoutMs);
    return true;
}

void HostUdp::setTimeout(int milliseconds)
{
    timeoutMs = milliseconds < 0 ? 0 : milliseconds;
    if (handle < 0) return;
    const SocketHandle s = static_cast<SocketHandle>(handle);
#if defined(_WIN32)
    const DWORD value = static_cast<DWORD>(timeoutMs);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value));
#else
    timeval value{};
    value.tv_sec = timeoutMs / 1000;
    value.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
#endif
}

bool HostUdp::listen(const std::string& host, int port, std::string& outError)
{
    addrinfo* info = nullptr;
    if (!lookup(host, port, SOCK_DGRAM, &info, outError))
    {
        return false;
    }
    const addrinfo* chosen = pick(info, family);
    if (!open(chosen->ai_family, outError))
    {
        freeaddrinfo(info);
        return false;
    }
    const SocketHandle s = static_cast<SocketHandle>(handle);
    const int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    if (::bind(s, chosen->ai_addr, static_cast<socklen_t>(chosen->ai_addrlen)) != 0)
    {
        outError = "cannot listen on " + addressText(chosen->ai_addr, static_cast<int>(chosen->ai_addrlen)) + ": " + lastError();
        freeaddrinfo(info);
        close();
        return false;
    }
    freeaddrinfo(info);

    sockaddr_storage bound{};
    socklen_t length = sizeof(bound);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &length) == 0)
    {
        localPort = (bound.ss_family == AF_INET6)
            ? ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port)
            : ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
    }
    return true;
}

bool HostUdp::point(const std::string& host, int port, std::string& outError)
{
    addrinfo* info = nullptr;
    if (!lookup(host, port, SOCK_DGRAM, &info, outError))
    {
        return false;
    }
    const addrinfo* chosen = pick(info, family);
    if (!open(chosen->ai_family, outError))
    {
        freeaddrinfo(info);
        return false;
    }
    std::memset(remote, 0, sizeof(remote));
    std::memcpy(remote, chosen->ai_addr, chosen->ai_addrlen);
    remoteLength = static_cast<int>(chosen->ai_addrlen);
    remoteText = addressText(chosen->ai_addr, remoteLength);
    freeaddrinfo(info);
    return true;
}

void HostUdp::close()
{
    if (handle >= 0)
    {
        closeSocket(static_cast<SocketHandle>(handle));
        handle = -1;
    }
    family = 0;
    localPort = 0;
    remoteLength = 0;
    remoteText.clear();
}

int HostUdp::receive(uint8_t* buffer, size_t capacity)
{
    if (handle < 0) return 0;
    const SocketHandle s = static_cast<SocketHandle>(handle);
    const int got = static_cast<int>(::recvfrom(s, reinterpret_cast<char*>(buffer),
#if defined(_WIN32)
                                                static_cast<int>(capacity),
#else
                                                capacity,
#endif
#if defined(MSG_TRUNC) && !defined(_WIN32)
                                                MSG_TRUNC,   // the real length, so an oversize is seen
#else
                                                0,
#endif
                                                nullptr, nullptr));
    if (got < 0)
    {
#if defined(_WIN32)
        const int code = WSAGetLastError();
        if (code == WSAEMSGSIZE) return -1;   // too big for the buffer: dropped
#endif
        return 0;   // a timeout, or the socket closing under us
    }
    if (static_cast<size_t>(got) > capacity)
    {
        return -1;   // truncated: dropped rather than decoded as garbage
    }
    return got;
}

bool HostUdp::send(const uint8_t* data, size_t length)
{
    if (handle < 0 || remoteLength == 0) return false;
    const SocketHandle s = static_cast<SocketHandle>(handle);
    const int sent = static_cast<int>(::sendto(s, reinterpret_cast<const char*>(data),
#if defined(_WIN32)
                                               static_cast<int>(length),
#else
                                               length,
#endif
                                               0, reinterpret_cast<const sockaddr*>(remote),
                                               static_cast<socklen_t>(remoteLength)));
    return sent == static_cast<int>(length);
}

#endif
