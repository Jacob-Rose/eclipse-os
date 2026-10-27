// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>
#include <vector>

#include "../ecore/core.h"

#include "frame.h"

namespace eio
{
    class RelicIO;
}

///
/// The relic's end of the link. A desk drives a relic two ways:
///
///   cue    a Command frame carries console text; the relic keeps rendering
///          its own looks.
///   pixel  a Pixels frame carries the picture; the relic becomes a display.
///
/// Pixel mode is a takeover that ends on its own: if no pixel frame arrives
/// for `holdoverSeconds` the relic takes its pixels back. A pulled cable costs
/// a look, not a dark sculpture.
///

namespace elink
{
    /* @brief Where bytes come from and where log lines go.
    *
    * The firmware plugs Serial in behind this (SerialLinkTransport); tests
    * plug in a byte queue.
    */
    class LinkTransport
    {
    public:
        virtual ~LinkTransport() = default;

        /// One byte, or -1 when nothing is waiting. Must not block.
        virtual int readByte() = 0;

        /// A text line back to the desk, on the relic's log stream.
        virtual void writeLine(const char* text) = 0;

        /// Reboot into the USB bootloader and do not come back. The default
        /// does nothing, which is right for a host.
        virtual void rebootToBootloader() {}
    };


    /* @brief Reads frames off a LinkTransport and applies them: pixels to a
    * RelicIO, commands to a queue the relic pops with takeCommand().
    *
    *     link.setTransport(&serial);
    *     link.setIdentity("obelisk");
    *     ...
    *     link.tick(deltaTime, io);
    *     if (!link.ownsPixels()) renderOwnLook();
    *     while (link.takeCommand(line)) handleCommand(line);
    */
    class RelicLink
    {
    public:
        /// Nothing happens until a transport is set.
        void setTransport(LinkTransport* inTransport) { transport = inTransport; }
        LinkTransport* getTransport() const { return transport; }

        /// Drains whatever has arrived. Pixel frames are written through to
        /// `io` (null: parsed, counted and dropped); commands queue.
        void tick(float deltaTime, eio::RelicIO* io);

        /// True while the desk owns the pixels; skip the relic's own rendering.
        bool ownsPixels() const { return streaming; }

        /// Pops one queued command, oldest first. False when there are none.
        bool takeCommand(std::string& outCommand);

        /// How long a takeover survives silence before the relic takes its
        /// pixels back.
        void setHoldover(float seconds) { holdoverSeconds = seconds; }
        float getHoldover() const { return holdoverSeconds; }

        /// Seconds since the last pixel frame, for a status line.
        float sinceLastFrame() const { return silence; }

        const FrameReader& getReader() const { return reader; }

        /// Pixel frames that named a strip this relic does not have.
        uint32_t framesUnrouted() const { return unrouted; }

        /// What the relic answers a Hello with; the desk prints it.
        void setIdentity(const std::string& inIdentity) { identity = inIdentity; }

    private:
        void handleFrame(eio::RelicIO* io);
        void applyPixels(eio::RelicIO* io);
        void answerHello(eio::RelicIO* io);
        void collectTyped(uint8_t byte);
        void queueCommand(const std::string& command);

        LinkTransport* transport{nullptr};
        FrameReader reader;

        bool streaming{false};
        float silence{0.0f};
        float holdoverSeconds{0.5f};

        uint32_t unrouted{0};

        std::string identity{"eclipse-os relic"};
        std::vector<std::string> commands;

        /// bytes typed by a human, gathered between frames; see collectTyped
        std::string typed;
    };
}
