// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <edmx/fixture.h>
#include <edmx/output.h>
#include <edmx/universe.h>
#include <elink/frame.h>

#include "edesk/serial_port.h"

///
/// The desk's DMX transports: USB widgets, a relic over elink, a console.
/// The buffer and the output interface they implement are the library's
/// (src/lib/edmx).
///

namespace edesk
{
    using edmx::DMX_CHANNEL_COUNT;
    using edmx::ChannelBuffer;
    using edmx::DmxUniverse;

    // the patch model, from the library too
    using edmx::Rgb8;
    using edmx::hsvToRgb8;
    using edmx::applyGamma;
    using edmx::Fixture;
    using edmx::resolveChannelOrder;
    using edmx::fixtureHighestChannel;
    using edmx::FixtureProfile;
    using edmx::lookupBuiltinProfile;
    using edmx::builtinProfileNames;
    using edmx::describeProfile;
    using edmx::instantiateProfile;
    using edmx::validateProfile;
    using edmx::FixtureMap;

    /* @brief An edmx::DmxOutput the show runner can ask whether it is a relic
    * on a USB cable, so cues can be forwarded to it. A virtual rather than a
    * cast: no RTTI, same as the library.
    */
    class ShowOutput : public edmx::DmxOutput
    {
    public:
        virtual class RelicUsbOutput* asRelicLink() { return nullptr; }
    };


    /// Enttec DMX USB PRO (and PRO Mk2 port 1).
    ///
    /// The widget has firmware that owns the DMX timing, so a frame is just a
    /// framed message over the virtual COM port:
    ///
    ///   0x7E | label 6 | len lo | len hi | start code + 512 channels | 0xE7
    ///
    class EnttecProOutput : public ShowOutput
    {
    public:
        EnttecProOutput(const std::string& inPort, int inBaud);

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const edmx::ChannelBuffer& universe, std::string& outError) override;
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
    class EnttecOpenOutput : public ShowOutput
    {
    public:
        explicit EnttecOpenOutput(const std::string& inPort);

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const edmx::ChannelBuffer& universe, std::string& outError) override;
        std::string describe() const override;
        float maxFrameRate() const override;
        void setUniverseLength(int channels) override;

    private:
        /// Shapes a frame by hand and hands it to whichever transport is open.
        bool sendVia(std::string& outError);

        std::string port;
        int universeLength{DMX_CHANNEL_COUNT};
        std::vector<uint8_t> packet;

        /// The fallback, and on Windows the only one.
        SerialPort serial;

        /// When the last frame was handed over, and how long it needs to clock
        /// out. The next break must not be asserted before then — that is the
        /// entire bug this class was rewritten for. Declared on every platform
        /// because open() computes frameSeconds unconditionally; only the
        /// libftdi path below actually paces on it.
        double lastWriteAt{0.0};
        double frameSeconds{0.0};

#if !defined(_WIN32)
        /// libftdi's context, when we got one. See edesk/ftdi_dmx.h for why this
        /// is preferred over the serial port rather than the other way around:
        /// the tty layer cannot say when a frame has left the chip, so the
        /// break lands inside the previous frame and the rig stays dark while
        /// every diagnostic reports success.
        void* ftdi{nullptr};
#endif
    };


    /// Renders frames to stderr instead of hardware. This is what makes the
    /// whole chain testable with no widget plugged in.
    class ConsoleOutput : public ShowOutput
    {
    public:
        explicit ConsoleOutput(int inChannelsShown);

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const edmx::ChannelBuffer& universe, std::string& outError) override;
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
    class RelicUsbOutput : public ShowOutput
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
        bool sendFrame(const edmx::ChannelBuffer& universe, std::string& outError) override;
        std::string describe() const override;
        void setUniverseLength(int channels) override;

        RelicUsbOutput* asRelicLink() override { return this; }

        /// Sends a line for the relic's own handleCommand. Works in both modes.
        bool sendCommand(const std::string& text, std::string& outError);

        /// Hands the pixels back now, rather than letting the relic time out.
        /// The stream stays off afterwards until take().
        bool release(std::string& outError);

        /// Starts sending frames again after a release, or after opening
        /// released. The takeover is the first frame; what that frame
        /// carries is the show's business - see edesk::RelicShadow for how a
        /// look makes it the relic's own picture.
        void take() { streaming = true; }

        /// True while pixel frames go out. Cue mode never streams.
        bool isStreaming() const { return mode == Mode::Pixels && streaming; }

        /// Whether the stream begins on the first frame (the default) or
        /// waits for take(). Set before open().
        void setStartReleased(bool released) { streaming = !released; }

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
        bool streaming{true};
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
    class PreviewOutput : public ShowOutput
    {
    public:
        explicit PreviewOutput(std::string inWhat) : what(std::move(inWhat)) {}

        bool open(std::string& outError) override;
        void close() override;
        bool isOpen() const override;
        bool sendFrame(const edmx::ChannelBuffer& universe, std::string& outError) override;
        std::string describe() const override;

    private:
        std::string what;
        bool opened{false};
    };


    /// Builds the output named by `type` ("enttec_pro", "enttec_open",
    /// "console", "preview"). Returns nullptr and fills outError on an unknown
    /// type.
    std::unique_ptr<ShowOutput> makeDmxOutput(const std::string& type,
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
