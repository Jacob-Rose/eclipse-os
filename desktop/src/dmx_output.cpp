// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/dmx_output.h"

#include <algorithm>
#include <cstdio>

using namespace edmx;

namespace
{
    // Enttec DMX USB PRO framing
    constexpr uint8_t ENTTEC_START_OF_MESSAGE = 0x7E;
    constexpr uint8_t ENTTEC_END_OF_MESSAGE   = 0xE7;
    constexpr uint8_t ENTTEC_LABEL_SEND_DMX   = 6;

    // DMX512 null start code: what a dimmer/LED fixture listens for.
    constexpr uint8_t DMX_START_CODE = 0x00;

    bool looksLikeDmxWidget(const SerialPortInfo& info)
    {
        // FTDI-backed widgets show up as ttyUSB on Linux and usbserial on mac.
        // On Windows we cannot tell from the port name alone, so anything goes
        // and the user gets to be explicit if the guess is wrong.
        const std::string& path = info.path;
        return path.rfind("/dev/ttyUSB", 0) == 0
            || path.rfind("/dev/cu.usbserial", 0) == 0
            || path.rfind("COM", 0) == 0;
    }
}

// ============================================================================
// Enttec DMX USB PRO
// ============================================================================

EnttecProOutput::EnttecProOutput(const std::string& inPort, int inBaud)
    : port(inPort), baud(inBaud)
{
    // start code + 512 channels is the payload the widget wants; anything
    // shorter and some fixtures never see their channel.
    const size_t payload = 1 + DMX_CHANNEL_COUNT;
    packet.resize(4 + payload + 1);
    packet[0] = ENTTEC_START_OF_MESSAGE;
    packet[1] = ENTTEC_LABEL_SEND_DMX;
    packet[2] = static_cast<uint8_t>(payload & 0xFF);
    packet[3] = static_cast<uint8_t>((payload >> 8) & 0xFF);
    packet[4] = DMX_START_CODE;
    packet.back() = ENTTEC_END_OF_MESSAGE;
}

bool EnttecProOutput::open(std::string& outError)
{
    return serial.open(port, baud, outError);
}

void EnttecProOutput::close()
{
    serial.close();
}

bool EnttecProOutput::isOpen() const
{
    return serial.isOpen();
}

bool EnttecProOutput::sendFrame(const DmxUniverse& universe, std::string& outError)
{
    if (!serial.isOpen())
    {
        outError = "enttec pro output is not open";
        return false;
    }

    std::copy(universe.data(), universe.data() + universe.size(), packet.begin() + 5);
    return serial.write(packet.data(), packet.size(), outError);
}

std::string EnttecProOutput::describe() const
{
    return "enttec_pro on " + port + " @ " + std::to_string(baud) + " baud";
}

// ============================================================================
// Enttec Open DMX USB
// ============================================================================

EnttecOpenOutput::EnttecOpenOutput(const std::string& inPort)
    : port(inPort)
{
    packet.resize(1 + DMX_CHANNEL_COUNT);
    packet[0] = DMX_START_CODE;
}

bool EnttecOpenOutput::open(std::string& outError)
{
    // The Open widget has no protocol layer: we drive the line at DMX's own
    // 250k 8N2 and shape the frame by hand.
    return serial.open(port, 250000, outError, 2);
}

void EnttecOpenOutput::close()
{
    serial.close();
}

bool EnttecOpenOutput::isOpen() const
{
    return serial.isOpen();
}

bool EnttecOpenOutput::sendFrame(const DmxUniverse& universe, std::string& outError)
{
    if (!serial.isOpen())
    {
        outError = "enttec open output is not open";
        return false;
    }

    // DMX512 frame: >=92us break, >=12us mark-after-break, then start code and
    // channel data. We ask for comfortably more than the minimum because host
    // scheduling jitter cuts the other way far more often than it pads.
    if (!serial.sendBreak(120, 20, outError))
    {
        return false;
    }

    std::copy(universe.data(), universe.data() + universe.size(), packet.begin() + 1);
    return serial.write(packet.data(), packet.size(), outError);
}

std::string EnttecOpenOutput::describe() const
{
    return "enttec_open on " + port + " @ 250000 baud (host-timed)";
}

// ============================================================================
// Console
// ============================================================================

ConsoleOutput::ConsoleOutput(int inChannelsShown)
    : channelsShown(std::max(0, std::min(inChannelsShown, DMX_CHANNEL_COUNT)))
{
}

bool ConsoleOutput::open(std::string& outError)
{
    (void)outError;
    opened = true;
    return true;
}

void ConsoleOutput::close()
{
    opened = false;
}

bool ConsoleOutput::isOpen() const
{
    return opened;
}

bool ConsoleOutput::sendFrame(const DmxUniverse& universe, std::string& outError)
{
    (void)outError;

    std::fprintf(stderr, "frame %6llu |", static_cast<unsigned long long>(frameCounter++));
    for (int channel = 1; channel <= channelsShown; ++channel)
    {
        std::fprintf(stderr, " %3u", static_cast<unsigned>(universe.getChannel(channel)));
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    return true;
}

std::string ConsoleOutput::describe() const
{
    return "console (no hardware, first " + std::to_string(channelsShown) + " channels to stderr)";
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<DmxOutput> edmx::makeDmxOutput(const std::string& type,
                                               const std::string& port,
                                               int baud,
                                               int consoleChannels,
                                               std::string& outError)
{
    if (type == "console" || type == "none" || type == "null")
    {
        return std::unique_ptr<DmxOutput>(new ConsoleOutput(consoleChannels));
    }
    if (type == "enttec_pro")
    {
        if (port.empty())
        {
            outError = "no serial port for enttec_pro (set device.port, or plug the widget in for \"auto\")";
            return nullptr;
        }
        return std::unique_ptr<DmxOutput>(new EnttecProOutput(port, baud));
    }
    if (type == "enttec_open")
    {
        if (port.empty())
        {
            outError = "no serial port for enttec_open (set device.port, or plug the widget in for \"auto\")";
            return nullptr;
        }
        return std::unique_ptr<DmxOutput>(new EnttecOpenOutput(port));
    }

    outError = "unknown device type '" + type + "' (expected enttec_pro, enttec_open or console)";
    return nullptr;
}

std::string edmx::autoDetectPort()
{
    for (const SerialPortInfo& info : SerialPort::enumeratePorts())
    {
        if (looksLikeDmxWidget(info))
        {
            return info.path;
        }
    }
    return std::string();
}
