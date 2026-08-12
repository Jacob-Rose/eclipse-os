// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lib/elink/frame.h"

#include "edmx/serial_port.h"

///
/// DMX universe buffer and the outputs that can push it at hardware.
///

namespace edmx
{
    static constexpr int DMX_CHANNEL_COUNT = 512;

    /// The frame buffer: every channel the rig owns, addressed the way fixture
    /// manuals number them, 1..N, and stored 0-based behind that.
    ///
    /// N is the *rig's*, not DMX's. On a DMX universe it is 512 and nothing
    /// ever asks for more. But the same buffer is what a relic's LED strip is
    /// rendered into, and the obelisk is 344 pixels at three slots each — 1032.
    /// A pixel rig is not sending DMX512 and is not bound by its slot count, so
    /// the limit lives on the outputs that actually speak DMX (they clamp at
    /// DMX_CHANNEL_COUNT) rather than on the buffer they read from.
    class DmxUniverse
    {
    public:
        DmxUniverse() : DmxUniverse(DMX_CHANNEL_COUNT) {}

        explicit DmxUniverse(int inChannelCount)
        {
            resize(inChannelCount);
        }

        /// Sizes the buffer to a rig. Clamped to at least one DMX universe, so
        /// a rig smaller than 512 still hands a full universe to a DMX widget.
        void resize(int inChannelCount)
        {
            channels.assign(static_cast<size_t>(std::max(inChannelCount, DMX_CHANNEL_COUNT)), 0);
        }

        void clear() { std::fill(channels.begin(), channels.end(), uint8_t{0}); }

        void setChannel(int channel1Based, uint8_t value)
        {
            if (channel1Based < 1 || static_cast<size_t>(channel1Based) > channels.size())
            {
                return;
            }
            channels[static_cast<size_t>(channel1Based - 1)] = value;
        }

        uint8_t getChannel(int channel1Based) const
        {
            if (channel1Based < 1 || static_cast<size_t>(channel1Based) > channels.size())
            {
                return 0;
            }
            return channels[static_cast<size_t>(channel1Based - 1)];
        }

        const uint8_t* data() const { return channels.data(); }
        size_t size() const { return channels.size(); }

    private:
        std::vector<uint8_t> channels;
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

        /// Send only the first `channels` slots, instead of all 512.
        ///
        /// A DMX frame does not have to carry the whole universe: a receiver
        /// takes what arrives and keeps what it already had. A rig using 70
        /// channels has no use for the 442 zeros behind them, and sending them
        /// anyway is the difference between a 519-byte frame and a 77-byte one
        /// — which on a serial link is most of the reason a rig cannot keep up.
        ///
        /// Clamped to a sane floor: some fixtures dislike very short frames.
        virtual void setUniverseLength(int channels) { (void)channels; }

        /// Frames per second this output can actually sustain, or 0 for "no
        /// limit worth enforcing".
        ///
        /// This matters more than it looks. A serial widget can only carry so
        /// many bytes a second, and asking for frames faster than that does not
        /// give a faster rig — it grows an unbounded backlog in the driver
        /// until the widget's parser loses sync, and the fixtures strobe on
        /// half-read frames. Better to render slower and correctly.
        virtual float maxFrameRate() const { return 0.0f; }

        virtual std::string describe() const = 0;

        /// Non-null when this output is a relic on a USB cable, so the command
        /// layer can forward cues to it. A virtual rather than a dynamic_cast
        /// for the same reason Pattern::asStateMachine is one: the library
        /// builds without RTTI on the microcontroller side and there is no
        /// reason for the two to diverge.
        virtual class RelicUsbOutput* asRelicLink() { return nullptr; }
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
        float maxFrameRate() const override;
        void setUniverseLength(int channels) override;

    private:
        void rebuildPacket();

        std::string port;
        int baud;
        int universeLength{DMX_CHANNEL_COUNT};
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
        float maxFrameRate() const override;
        void setUniverseLength(int channels) override;

    private:
        std::string port;
        int universeLength{DMX_CHANNEL_COUNT};
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


    /// An eclipse-os relic on the other end of a USB cable.
    ///
    /// Not DMX at all. The frame buffer is already the pixel buffer — three
    /// consecutive channels per pixel, in strip order — so a frame is that
    /// buffer wrapped in an elink header and put on the wire. The relic writes
    /// it straight to its LEDs and hands its own patterns back when we stop.
    ///
    /// Two modes, and they are not alternatives:
    ///
    ///   Pixels  we render and the relic displays. Any desk look reaches the
    ///           sculpture, including ones never compiled into it.
    ///   Cue     we send `state x` and the relic renders its own looks. Almost
    ///           no bandwidth, and it survives a cable nobody trusts.
    ///
    /// Cues are sent in either mode: arming the look a relic will fall back to
    /// is exactly what you want set before a stream drops.
    class RelicUsbOutput : public DmxOutput
    {
    public:
        enum class Mode
        {
            Pixels,
            Cue
        };

        RelicUsbOutput(const std::string& inPort, int inBaud, Mode inMode);
        ~RelicUsbOutput() override;

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const DmxUniverse& universe, std::string& outError) override;
        std::string describe() const override;
        void setUniverseLength(int channels) override;

        RelicUsbOutput* asRelicLink() override { return this; }

        /// Sends a line for the relic's own handleCommand. Works in both modes.
        bool sendCommand(const std::string& text, std::string& outError);

        /// Hands the pixels back now, rather than letting the relic time out.
        bool release(std::string& outError);

        /// Reboots the relic into its USB bootloader so it can be reflashed.
        /// The relic does not come back; the port disappears.
        bool rebootToBootloader(std::string& outError);

        void setMode(Mode inMode) { mode = inMode; }
        Mode getMode() const { return mode; }

        /// Lines the relic has sent since the last call, log and all. Never
        /// waits; an empty vector means it has not said anything yet.
        std::vector<std::string> drainRelicLines();

    private:
        bool sendRaw(const uint8_t* payload, uint16_t length, uint8_t type, std::string& outError);

        /// How many writes in a row may fail before the show gives up. At 30fps
        /// this is about two thirds of a second, which is longer than any
        /// stutter and shorter than anyone would stand looking at a frozen rig.
        static constexpr int MAX_CONSECUTIVE_FAILURES = 20;

        std::string port;
        int baud;
        Mode mode;
        int pixelCount{0};
        int consecutiveFailures{0};
        SerialPort serial;

        std::vector<uint8_t> packet;  // reused every frame, no per-frame allocation
        std::vector<uint8_t> payload;
        std::string inbound;          // partial line from the relic
    };


    /// No wire at all: renders, and drops the frame on the floor.
    ///
    /// This is the honest output for a rig whose destination is the interface
    /// rather than hardware — the obelisk, until the USB link to it exists.
    /// `console` would do the job but prints a line per frame to stderr, which
    /// buries the log of a show you are actually watching in the viewer; and
    /// unlike `console` this does not pretend the rig is a DMX one.
    class PreviewOutput : public DmxOutput
    {
    public:
        explicit PreviewOutput(std::string inWhat) : what(std::move(inWhat)) {}

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const DmxUniverse& universe, std::string& outError) override;
        std::string describe() const override;

    private:
        std::string what;
        bool opened{false};
    };


    /// Builds the output named by `type` ("enttec_pro", "enttec_open",
    /// "console", "preview"). Returns nullptr and fills outError on an unknown
    /// type.
    std::unique_ptr<DmxOutput> makeDmxOutput(const std::string& type,
                                             const std::string& port,
                                             int baud,
                                             int consoleChannels,
                                             std::string& outError);

    /// Picks the first attached serial port that looks like a DMX widget, for
    /// `"port": "auto"`. Empty string when nothing plausible is present.
    std::string autoDetectPort();


    struct RelicProbe
    {
        std::string port;
        std::string identity; ///< what the relic answered, minus the prefix
    };

    /// Asks every attached port whether it is a relic, and reports the ones
    /// that answer.
    ///
    /// A guess from the port name is not available here. A relic is a Pico on
    /// USB CDC and a DMX widget is an FTDI part, and on Windows both are just
    /// "COMn" with a description out of the registry that names neither — this
    /// machine's widget reports itself as `\Device\VCP0`. Picking the wrong one
    /// would mean a show quietly driving a widget that ignores it.
    ///
    /// So we ask. A Hello is seven bytes and a relic answers it by name, which
    /// makes this the only detection that cannot be wrong. Anything that does
    /// not answer inside `millisecondsEach` is not a relic.
    ///
    /// The seven bytes do reach whatever is on the other end. On a DMX widget
    /// they are data with no break in front of them, which every receiver
    /// ignores by construction — but it is the reason this is a deliberate step
    /// rather than something that happens on every startup.
    std::vector<RelicProbe> probeRelicPorts(int baud, int millisecondsEach = 250);

    /// First port that answers a Hello. Empty when none do.
    std::string autoDetectRelicPort(int baud);
}
