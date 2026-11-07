// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "mqtt_transport_host.h"

#if !USE_ARDUINO

#include <ecore/logging.h>

#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
#endif

using namespace emqtt;
using namespace ecore::log;

namespace
{
#if defined(_WIN32)
    using SocketHandle = SOCKET;

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

    bool wouldBlock()
    {
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
    }

    void setNonBlocking(SocketHandle s)
    {
        u_long enabled = 1;
        ioctlsocket(s, FIONBIO, &enabled);
    }

    int pollSocket(WSAPOLLFD& entry, int milliseconds) { return WSAPoll(&entry, 1, milliseconds); }
    using PollEntry = WSAPOLLFD;
    void closeHandle(SocketHandle s) { closesocket(s); }
#else
    using SocketHandle = int;

    void startup() {}

    bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }

    void setNonBlocking(SocketHandle s)
    {
        ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    }

    int pollSocket(pollfd& entry, int milliseconds) { return ::poll(&entry, 1, milliseconds); }
    using PollEntry = pollfd;
    void closeHandle(SocketHandle s) { ::close(s); }
#endif

    /// True when the socket is ready for `events` within the wait.
    bool waitFor(intptr_t handle, short events, int milliseconds)
    {
        PollEntry entry{};
        entry.fd = static_cast<SocketHandle>(handle);
        entry.events = events;
        return pollSocket(entry, milliseconds) > 0;
    }
}

MqttTransportHost::~MqttTransportHost()
{
    closeSocket();
}

bool MqttTransportHost::init(const char* inServer, uint16_t inPort, const char* inClientId,
                             const char* inUsername, const char* inPassword)
{
    server = inServer;
    port = inPort;
    clientId = inClientId;
    bHasUsername = inUsername != nullptr;
    bHasPassword = inPassword != nullptr;
    username = inUsername ? inUsername : "";
    password = inPassword ? inPassword : "";
    return true;
}

bool MqttTransportHost::openSocket()
{
    startup();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(server.c_str(), std::to_string(port).c_str(), &hints, &found) != 0)
    {
        return false;
    }

    bool opened = false;
    for (addrinfo* at = found; at != nullptr && !opened; at = at->ai_next)
    {
        const SocketHandle s = ::socket(at->ai_family, at->ai_socktype, at->ai_protocol);
        if (static_cast<intptr_t>(s) < 0)
        {
            continue;
        }

        setNonBlocking(s);
#ifdef SO_NOSIGPIPE
        const int noSigpipe = 1;
        ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
#endif

        bool connected = ::connect(s, at->ai_addr, static_cast<int>(at->ai_addrlen)) == 0;
        if (!connected && wouldBlock() && waitFor(static_cast<intptr_t>(s), POLLOUT, timeoutMs))
        {
            int error = 0;
            socklen_t length = sizeof(error);
            ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
            connected = error == 0;
        }

        if (connected)
        {
            const int noDelay = 1;
            ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
            handle = static_cast<intptr_t>(s);
            opened = true;
        }
        else
        {
            closeHandle(s);
        }
    }

    freeaddrinfo(found);
    return opened;
}

bool MqttTransportHost::connect()
{
    if (isConnected())
    {
        return true;
    }

    closeSocket();
    if (!openSocket())
    {
        dbgLog("MQTT: could not reach the broker", Verbosity::Error, Category::IO);
        return false;
    }

    const std::vector<uint8_t> hello = encodeConnect(clientId.c_str(),
                                                     bHasUsername ? username.c_str() : nullptr,
                                                     bHasPassword ? password.c_str() : nullptr,
                                                     keepAlive);
    if (!sendBytes(hello) || !awaitConnAck())
    {
        closeSocket();
        return false;
    }

    bSessionUp = true;
    bPingOutstanding = false;
    lastSentAt = millis();
    return true;
}

bool MqttTransportHost::awaitConnAck()
{
    const uint32_t started = millis();
    while (handle >= 0)
    {
        const int elapsed = static_cast<int>(millis() - started);
        if (elapsed >= timeoutMs || !waitFor(handle, POLLIN, timeoutMs - elapsed))
        {
            dbgLog("MQTT: the broker did not answer", Verbosity::Error, Category::IO);
            return false;
        }

        uint8_t bytes[64];
        const auto count = ::recv(static_cast<SocketHandle>(handle), reinterpret_cast<char*>(bytes), sizeof(bytes), 0);
        if (count <= 0)
        {
            return false;
        }
        reader.feed(bytes, static_cast<size_t>(count));

        MqttPacket packet;
        if (reader.next(packet))
        {
            if (packet.type != MqttPacketType::ConnAck || packet.body.size() < 2 || packet.body[1] != 0)
            {
                dbgLog("MQTT: the broker refused the connection", Verbosity::Error, Category::IO);
                return false;
            }
            return true;
        }
    }
    return false;
}

void MqttTransportHost::disconnect()
{
    if (bSessionUp)
    {
        sendBytes(encodeDisconnect());
    }
    closeSocket();
}

bool MqttTransportHost::isConnected()
{
    return handle >= 0 && bSessionUp;
}

bool MqttTransportHost::publish(const char* topic, const char* payload, bool retained)
{
    return isConnected() && sendBytes(encodePublish(topic, payload, retained));
}

bool MqttTransportHost::subscribe(const char* topic, uint8_t qos)
{
    if (!isConnected())
    {
        return false;
    }
    const uint16_t packetId = nextPacketId;
    nextPacketId = nextPacketId == 0xFFFF ? 1 : nextPacketId + 1;
    return sendBytes(encodeSubscribe(packetId, topic, qos));
}

bool MqttTransportHost::sendBytes(const std::vector<uint8_t>& bytes)
{
    size_t sent = 0;
    while (sent < bytes.size())
    {
        const auto count = ::send(static_cast<SocketHandle>(handle),
                                  reinterpret_cast<const char*>(bytes.data()) + sent,
                                  static_cast<int>(bytes.size() - sent), MSG_NOSIGNAL);
        if (count > 0)
        {
            sent += static_cast<size_t>(count);
        }
        else if (!wouldBlock() || !waitFor(handle, POLLOUT, timeoutMs))
        {
            closeSocket();
            return false;
        }
    }
    lastSentAt = millis();
    return true;
}

void MqttTransportHost::loop()
{
    if (!isConnected())
    {
        return;
    }

    while (true)
    {
        uint8_t bytes[1024];
        const auto count = ::recv(static_cast<SocketHandle>(handle), reinterpret_cast<char*>(bytes), sizeof(bytes), 0);
        if (count > 0)
        {
            reader.feed(bytes, static_cast<size_t>(count));
            continue;
        }
        if (count == 0 || !wouldBlock())
        {
            closeSocket();
            return;
        }
        break;
    }

    MqttPacket packet;
    while (handle >= 0 && reader.next(packet))
    {
        handlePacket(packet);
    }
    if (reader.failed())
    {
        closeSocket();
        return;
    }
    if (handle < 0 || keepAlive == 0)
    {
        return;
    }

    const uint32_t now = millis();
    const uint32_t keepAliveMs = static_cast<uint32_t>(keepAlive) * 1000u;
    if (bPingOutstanding)
    {
        if (now - pingSentAt >= keepAliveMs)
        {
            dbgLog("MQTT: the broker stopped answering", Verbosity::Error, Category::IO);
            closeSocket();
        }
    }
    else if (now - lastSentAt >= keepAliveMs)
    {
        bPingOutstanding = true;
        pingSentAt = now;
        sendBytes(encodePingReq());
    }
}

void MqttTransportHost::handlePacket(MqttPacket& packet)
{
    if (packet.type == MqttPacketType::PingResp)
    {
        bPingOutstanding = false;
    }
    else if (packet.type == MqttPacketType::Publish)
    {
        MqttPublish message;
        if (!parsePublish(packet, message))
        {
            return;
        }
        if (message.qos == 1)
        {
            sendBytes(encodePubAck(message.packetId));
        }
        if (userCallback)
        {
            userCallback(message.topic.c_str(), message.payload, static_cast<unsigned int>(message.length));
        }
    }
}

void MqttTransportHost::closeSocket()
{
    if (handle >= 0)
    {
        closeHandle(static_cast<SocketHandle>(handle));
    }
    handle = -1;
    bSessionUp = false;
    bPingOutstanding = false;
    reader.reset();
}

#endif
