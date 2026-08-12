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
    rebuildPacket();
}

void EnttecProOutput::rebuildPacket()
{
    const size_t payload = 1 + static_cast<size_t>(universeLength);
    packet.assign(4 + payload + 1, 0);
    packet[0] = ENTTEC_START_OF_MESSAGE;
    packet[1] = ENTTEC_LABEL_SEND_DMX;
    packet[2] = static_cast<uint8_t>(payload & 0xFF);
    packet[3] = static_cast<uint8_t>((payload >> 8) & 0xFF);
    packet[4] = DMX_START_CODE;
    packet.back() = ENTTEC_END_OF_MESSAGE;
}

void EnttecProOutput::setUniverseLength(int channels)
{
    // The widget's send-DMX request takes 25..513 bytes of payload, so the
    // shortest legal universe is 24 channels. Nothing patched that low needs
    // the difference anyway.
    universeLength = std::clamp(channels, 24, DMX_CHANNEL_COUNT);
    rebuildPacket();
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

    std::copy(universe.data(), universe.data() + universeLength, packet.begin() + 5);
    return serial.write(packet.data(), packet.size(), outError);
}

std::string EnttecProOutput::describe() const
{
    return "enttec_pro on " + port + " @ " + std::to_string(baud) + " baud, "
         + std::to_string(universeLength) + " channels";
}

float EnttecProOutput::maxFrameRate() const
{
    // 8N1 is ten bits on the wire per byte, start and stop included. At 115200
    // a 519-byte packet is 45ms, so the honest ceiling is about 22fps - which
    // is why asking for 40 there strobed the rig rather than speeding it up.
    const float bitsPerFrame = 10.0f * static_cast<float>(packet.size());
    return static_cast<float>(baud) / bitsPerFrame;
}

// ============================================================================
// Enttec Open DMX USB
// ============================================================================

EnttecOpenOutput::EnttecOpenOutput(const std::string& inPort)
    : port(inPort)
{
    packet.assign(1 + DMX_CHANNEL_COUNT, 0);
    packet[0] = DMX_START_CODE;
}

void EnttecOpenOutput::setUniverseLength(int channels)
{
    universeLength = std::clamp(channels, 24, DMX_CHANNEL_COUNT);
    packet.assign(1 + static_cast<size_t>(universeLength), 0);
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

    // The break, the mark and the frame are one indivisible thing to a
    // receiver: a gap in the middle reads as a new break and the frame lands
    // shifted, which is what an occasional unexplained flicker is. Hold the
    // scheduler off for the whole of it rather than just the break.
    TimeCriticalSection timeCritical;

    // DMX512 frame: >=92us break, >=12us mark-after-break, then start code and
    // channel data. We ask for comfortably more than the minimum because host
    // scheduling jitter cuts the other way far more often than it pads.
    if (!serial.sendBreak(176, 24, outError))
    {
        return false;
    }

    std::copy(universe.data(), universe.data() + universeLength, packet.begin() + 1);
    return serial.write(packet.data(), packet.size(), outError);
}

std::string EnttecOpenOutput::describe() const
{
    return "enttec_open on " + port + " @ 250000 baud (host-timed), "
         + std::to_string(universeLength) + " channels";
}

float EnttecOpenOutput::maxFrameRate() const
{
    // 8N2 is eleven bits per byte, plus the break and mark we hold before each
    // frame. That lands near DMX512's own ceiling of about 44 frames a second.
    const float bitsPerFrame = 11.0f * static_cast<float>(packet.size());
    const float frameSeconds = (bitsPerFrame / 250000.0f) + 0.00014f;
    return 1.0f / frameSeconds;
}

// ============================================================================
// Console
// ============================================================================

ConsoleOutput::ConsoleOutput(int inChannelsShown)
    // Not capped at DMX_CHANNEL_COUNT: a pixel rig's buffer is longer than a
    // universe, and asking to see channel 900 of one is a reasonable thing to
    // do. Past the end of the buffer getChannel reads 0, which is the truth.
    : channelsShown(std::max(0, inChannelsShown))
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
// Preview
// ============================================================================

bool PreviewOutput::open(std::string& outError)
{
    (void)outError;
    opened = true;
    return true;
}

void PreviewOutput::close()
{
    opened = false;
}

bool PreviewOutput::isOpen() const
{
    return opened;
}

bool PreviewOutput::sendFrame(const DmxUniverse& universe, std::string& outError)
{
    (void)universe;
    (void)outError;
    return true;
}

std::string PreviewOutput::describe() const
{
    return "preview (nothing on a wire; " + what + ")";
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
    if (type == "preview")
    {
        return std::unique_ptr<DmxOutput>(new PreviewOutput("watch it with --emit-frames"));
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

    outError = "unknown device type '" + type
             + "' (expected enttec_pro, enttec_open, console or preview)";
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
