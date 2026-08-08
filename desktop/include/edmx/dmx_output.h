// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "edmx/serial_port.h"

///
/// DMX universe buffer and the outputs that can push it at hardware.
///

namespace edmx
{
    static constexpr int DMX_CHANNEL_COUNT = 512;

    /// A single DMX512 universe. Channels are addressed the way the fixture
    /// manuals do, 1..512, and stored 0-based behind that.
    class DmxUniverse
    {
    public:
        DmxUniverse() { clear(); }

        void clear() { channels.fill(0); }

        void setChannel(int channel1Based, uint8_t value)
        {
            if (channel1Based < 1 || channel1Based > DMX_CHANNEL_COUNT)
            {
                return;
            }
            channels[static_cast<size_t>(channel1Based - 1)] = value;
        }

        uint8_t getChannel(int channel1Based) const
        {
            if (channel1Based < 1 || channel1Based > DMX_CHANNEL_COUNT)
            {
                return 0;
            }
            return channels[static_cast<size_t>(channel1Based - 1)];
        }

        const uint8_t* data() const { return channels.data(); }
        size_t size() const { return channels.size(); }

    private:
        std::array<uint8_t, DMX_CHANNEL_COUNT> channels{};
    };


    /// Anything that can take a universe and put it on a wire (or on stdout).
    class DmxOutput
    {
    public:
        virtual ~DmxOutput() = default;

        virtual bool open(std::string& outError) = 0;
        virtual void close() = 0;
        virtual bool isOpen() const = 0;

        /// Push one frame. Returning false ends the show with outError.
        virtual bool sendFrame(const DmxUniverse& universe, std::string& outError) = 0;

        virtual std::string describe() const = 0;
    };


    /// Enttec DMX USB PRO (and PRO Mk2 port 1).
    ///
    /// The widget has firmware that owns the DMX timing, so a frame is just a
    /// framed message over the virtual COM port:
    ///
    ///   0x7E | label 6 | len lo | len hi | start code + 512 channels | 0xE7
    ///
    class EnttecProOutput : public DmxOutput
    {
    public:
        EnttecProOutput(const std::string& inPort, int inBaud);

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const DmxUniverse& universe, std::string& outError) override;
        std::string describe() const override;

    private:
        std::string port;
        int baud;
        SerialPort serial;
        std::vector<uint8_t> packet; // reused every frame, no per-frame allocation
    };


    /// Enttec Open DMX USB.
    ///
    /// No firmware on this one: it is a bare FTDI chip wired to a driver, so
    /// the host has to generate the break and mark-after-break itself and clock
    /// the data out at DMX's native 250k 8N2. Timing is therefore at the mercy
    /// of the OS scheduler, which is exactly why the PRO exists. Supported
    /// because the hardware is cheap and common, but the PRO is the better path.
    class EnttecOpenOutput : public DmxOutput
    {
    public:
        explicit EnttecOpenOutput(const std::string& inPort);

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const DmxUniverse& universe, std::string& outError) override;
        std::string describe() const override;

    private:
        std::string port;
        SerialPort serial;
        std::vector<uint8_t> packet;
    };


    /// Renders frames to stderr instead of hardware. This is what makes the
    /// whole chain testable with no widget plugged in.
    class ConsoleOutput : public DmxOutput
    {
    public:
        explicit ConsoleOutput(int inChannelsShown);

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const DmxUniverse& universe, std::string& outError) override;
        std::string describe() const override;

    private:
        int channelsShown;
        bool opened{false};
        uint64_t frameCounter{0};
    };


    /// Builds the output named by `type` ("enttec_pro", "enttec_open",
    /// "console"). Returns nullptr and fills outError on an unknown type.
    std::unique_ptr<DmxOutput> makeDmxOutput(const std::string& type,
                                             const std::string& port,
                                             int baud,
                                             int consoleChannels,
                                             std::string& outError);

    /// Picks the first attached serial port that looks like a DMX widget, for
    /// `"port": "auto"`. Empty string when nothing plausible is present.
    std::string autoDetectPort();
}
