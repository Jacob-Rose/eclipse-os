// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/dmx_output.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace edmx;

namespace
{
    /// How long a USB CDC port needs after opening before it will carry a byte.
    ///
    /// Found the hard way: --probe-relics reported nothing on a relic that was
    /// running perfectly and answered the identical frame sent by hand a moment
    /// later. Nothing is wrong with the device; the host simply has not
    /// finished bringing the line up when CreateFile returns, and what you
    /// write into that gap is gone.
    constexpr int CDC_SETTLE_MS = 300;

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
// Relic over USB
// ============================================================================

RelicUsbOutput::RelicUsbOutput(const std::string& inPort, int inBaud, Mode inMode)
    : port(inPort), baud(inBaud), mode(inMode)
{
    packet.resize(elink::HEADER_SIZE + elink::MAX_PAYLOAD + elink::TRAILER_SIZE);
}

RelicUsbOutput::~RelicUsbOutput()
{
    // Give the pixels back on the way out rather than leaving the relic to
    // notice we stopped. A show that ends should end, not fade to a timeout.
    if (serial.isOpen())
    {
        std::string ignored;
        release(ignored);
    }
}

void RelicUsbOutput::setUniverseLength(int channels)
{
    // Three channels a pixel. A patch that does not divide evenly is a config
    // that is not describing a pixel strip, and the extra channels have nowhere
    // to go, so round down rather than send a partial pixel.
    pixelCount = std::max(0, channels / 3);

    const size_t needed = elink::PIXEL_HEADER_SIZE + static_cast<size_t>(pixelCount) * 3;
    payload.assign(needed, 0);
}

bool RelicUsbOutput::open(std::string& outError)
{
    // Never 1200. On an RP2040 that rate is the "touch to reboot into BOOTSEL"
    // signal the uploader uses, so opening a relic at it would drop the
    // sculpture into a bootloader instead of driving it.
    const int safeBaud = (baud == 1200) ? 115200 : baud;

    if (!serial.open(port, safeBaud, outError, 1, /*assertDtr=*/true))
    {
        return false;
    }

    consecutiveFailures = 0;

    // The port is not ready to carry anything yet - see CDC_SETTLE_MS. Without
    // this the Hello below is dropped, and so are the first frames of the show.
    std::this_thread::sleep_for(std::chrono::milliseconds(CDC_SETTLE_MS));

    // Say hello before anything else. The relic answers with what it actually
    // is - its strips and their lengths - so a mismatch between this config and
    // that sculpture shows up in the log at load-in rather than as a
    // half-lit rig during a set.
    std::string ignored;
    sendRaw(nullptr, 0, static_cast<uint8_t>(elink::FrameType::Hello), ignored);
    return true;
}

void RelicUsbOutput::close()
{
    if (serial.isOpen())
    {
        std::string ignored;
        release(ignored);
    }
    serial.close();
}

bool RelicUsbOutput::isOpen() const
{
    return serial.isOpen();
}

bool RelicUsbOutput::sendRaw(const uint8_t* inPayload, uint16_t length, uint8_t type, std::string& outError)
{
    if (!serial.isOpen())
    {
        outError = "relic link is not open";
        return false;
    }

    const size_t written = elink::writeFrame(static_cast<elink::FrameType>(type),
                                             inPayload, length,
                                             packet.data(), packet.size());
    if (written == 0)
    {
        outError = "elink frame of " + std::to_string(length) + " bytes could not be built";
        return false;
    }

    if (serial.write(packet.data(), written, outError))
    {
        consecutiveFailures = 0;
        return true;
    }

    // A write that times out is usually the relic being briefly behind rather
    // than gone: USB CDC is flow-controlled, so a device that has not drained
    // its buffer stops the host writing until it does. Ending a show over one
    // slow frame would be absurd - the relic still has the last frame on it and
    // will keep it for the holdover.
    //
    // Sustained failure is different, and after about a second of it the cable
    // really is out. Then it is an error, and the show says so.
    ++consecutiveFailures;
    if (consecutiveFailures < MAX_CONSECUTIVE_FAILURES)
    {
        outError.clear();
        return true;
    }

    outError = "relic on " + port + " stopped taking frames after "
             + std::to_string(consecutiveFailures) + " tries: " + outError;
    return false;
}

bool RelicUsbOutput::sendFrame(const DmxUniverse& universe, std::string& outError)
{
    if (mode == Mode::Cue)
    {
        // The relic is rendering its own looks. Sending it pixels as well would
        // take them away from it, which is the opposite of what cue mode is.
        return true;
    }

    if (pixelCount <= 0)
    {
        outError = "relic link has no pixels to send (patch resolves to " + std::to_string(pixelCount) + ")";
        return false;
    }

    elink::PixelHeader header;
    header.strip = 0;
    header.start = 0;
    header.count = static_cast<uint16_t>(pixelCount);
    elink::writePixelHeader(header, payload.data());

    std::copy(universe.data(),
              universe.data() + static_cast<size_t>(pixelCount) * 3,
              payload.begin() + elink::PIXEL_HEADER_SIZE);

    return sendRaw(payload.data(), static_cast<uint16_t>(payload.size()),
                   static_cast<uint8_t>(elink::FrameType::Pixels), outError);
}

bool RelicUsbOutput::sendCommand(const std::string& text, std::string& outError)
{
    if (text.empty() || text.size() > elink::MAX_PAYLOAD)
    {
        outError = "relic command must be 1.." + std::to_string(elink::MAX_PAYLOAD) + " characters";
        return false;
    }

    return sendRaw(reinterpret_cast<const uint8_t*>(text.data()),
                   static_cast<uint16_t>(text.size()),
                   static_cast<uint8_t>(elink::FrameType::Command), outError);
}

bool RelicUsbOutput::release(std::string& outError)
{
    return sendRaw(nullptr, 0, static_cast<uint8_t>(elink::FrameType::Release), outError);
}

bool RelicUsbOutput::rebootToBootloader(std::string& outError)
{
    if (!sendRaw(nullptr, 0, static_cast<uint8_t>(elink::FrameType::Reboot), outError))
    {
        return false;
    }

    // The relic is gone the moment it reads that, so there is nothing left to
    // hold the port open for - and leaving it open means the destructor's
    // Release write fails into a device that is no longer there.
    serial.close();
    return true;
}

std::vector<std::string> RelicUsbOutput::drainRelicLines()
{
    std::vector<std::string> lines;
    if (!serial.isOpen())
    {
        return lines;
    }

    uint8_t chunk[512];
    for (int pass = 0; pass < 8; ++pass)
    {
        const int got = serial.readAvailable(chunk, sizeof(chunk));
        if (got <= 0)
        {
            break;
        }

        for (int i = 0; i < got; ++i)
        {
            const char c = static_cast<char>(chunk[i]);
            if (c == '\n' || c == '\r')
            {
                if (!inbound.empty())
                {
                    lines.push_back(inbound);
                    inbound.clear();
                }
            }
            else if (inbound.size() < 512)
            {
                inbound.push_back(c);
            }
            else
            {
                // A relic mid-reboot emits a lot before it emits a newline.
                inbound.clear();
            }
        }
    }

    return lines;
}

std::string RelicUsbOutput::describe() const
{
    return std::string("relic_usb on ") + port + ", "
         + (mode == Mode::Cue ? "cue" : "pixel") + " mode, "
         + std::to_string(pixelCount) + " pixels";
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
    if (type == "relic_usb" || type == "relic_usb_cue")
    {
        if (port.empty())
        {
            outError = "no serial port for relic_usb (set device.port, or plug the relic in for \"auto\")";
            return nullptr;
        }
        const RelicUsbOutput::Mode mode = (type == "relic_usb_cue")
            ? RelicUsbOutput::Mode::Cue
            : RelicUsbOutput::Mode::Pixels;
        return std::unique_ptr<DmxOutput>(new RelicUsbOutput(port, baud, mode));
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
             + "' (expected enttec_pro, enttec_open, relic_usb, relic_usb_cue,"
               " console or preview)";
    return nullptr;
}

std::vector<RelicProbe> edmx::probeRelicPorts(int baud, int millisecondsEach)
// millisecondsEach is the listen window *after* the settle above, so the wall
// clock cost per port is CDC_SETTLE_MS + however long the relic takes to
// answer, which on a 30ms tick is one or two.
{
    static const std::string kPrefix = "EOSLINK hello ";

    std::vector<RelicProbe> found;

    // See RelicUsbOutput::open: 1200 reboots an RP2040 into its bootloader, and
    // a probe that bricks a show mid-load-in would be a memorable bug.
    const int safeBaud = (baud == 1200) ? 115200 : baud;

    for (const SerialPortInfo& info : SerialPort::enumeratePorts())
    {
        SerialPort probe;
        std::string error;
        // DTR on: a relic is a CDC device and will not answer without it.
        if (!probe.open(info.path, safeBaud, error, 1, /*assertDtr=*/true))
        {
            // Held by something else, or not openable. Not a relic today.
            continue;
        }

        // A CDC port is not ready the instant CreateFile returns. The host has
        // to assert DTR and the device has to notice, and anything written
        // before that lands nowhere - which reads exactly like a relic that
        // did not answer. Measured on a Pico: an immediate write is lost, and
        // it is reliable a few hundred milliseconds later.
        std::this_thread::sleep_for(std::chrono::milliseconds(CDC_SETTLE_MS));

        uint8_t frame[elink::HEADER_SIZE + elink::TRAILER_SIZE];
        const size_t written = elink::writeFrame(elink::FrameType::Hello, nullptr, 0,
                                                 frame, sizeof(frame));
        if (written == 0 || !probe.write(frame, written, error))
        {
            probe.close();
            continue;
        }

        // Poll rather than sleep the whole budget: a relic answers inside a
        // tick or two, and waiting the full window on every port would make
        // this take as long as there are ports.
        std::string buffered;
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(millisecondsEach);

        while (std::chrono::steady_clock::now() < deadline)
        {
            uint8_t chunk[256];
            const int got = probe.readAvailable(chunk, sizeof(chunk));
            if (got > 0)
            {
                buffered.append(reinterpret_cast<const char*>(chunk), static_cast<size_t>(got));

                const size_t at = buffered.find(kPrefix);
                if (at != std::string::npos)
                {
                    const size_t end = buffered.find_first_of("\r\n", at);
                    if (end != std::string::npos)
                    {
                        RelicProbe result;
                        result.port = info.path;
                        result.identity = buffered.substr(at + kPrefix.size(),
                                                          end - at - kPrefix.size());
                        found.push_back(result);
                        break;
                    }
                }
            }
            else if (got < 0)
            {
                break;
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        probe.close();
    }

    return found;
}

std::string edmx::autoDetectRelicPort(int baud)
{
    const std::vector<RelicProbe> found = probeRelicPorts(baud);
    return found.empty() ? std::string() : found.front().port;
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
