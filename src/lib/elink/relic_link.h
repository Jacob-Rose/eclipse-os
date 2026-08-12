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
/// The relic's end of the link.
///
/// A desk can drive a relic two ways and they are not alternatives:
///
///   cue    a Command frame carries the same text the serial console already
///          takes, and the relic goes on rendering its own looks. A few bytes,
///          occasionally, and nothing to keep up with.
///
///   pixel  a Pixels frame carries the frame itself and the relic becomes a
///          display. Any look the desk can render lands on the sculpture,
///          including ones that were never compiled into it.
///
/// Pixel mode is a *takeover*, and the important part is how it ends. The relic
/// does not wait to be told: if no pixel frame has arrived for `holdoverSeconds`
/// it takes its own pixels back and carries on. A pulled USB cable should cost
/// you a look, not a dark sculpture in front of a room.
///

namespace elink
{
    /// Where bytes come from and where log lines go.
    ///
    /// Abstract so the whole link can run on a host with no hardware: the
    /// firmware plugs Serial in behind this, the tests plug a byte queue in,
    /// and the code between them is the same code.
    class LinkTransport
    {
    public:
        virtual ~LinkTransport() = default;

        /// One byte, or -1 when there is nothing waiting. Must not block.
        virtual int readByte() = 0;

        /// A line back to the desk. Text, not frames: the relic's log stream is
        /// already text and a human already reads it over the same cable.
        virtual void writeLine(const char* text) = 0;
    };


    class RelicLink
    {
    public:
        /// Nothing happens until a transport is set, which is what keeps the
        /// link off relics that have not asked for it.
        void setTransport(LinkTransport* inTransport) { transport = inTransport; }
        LinkTransport* getTransport() const { return transport; }

        /// Drains whatever has arrived and applies it. Pixel frames are written
        /// through to `io` as they land; commands queue for takeCommand.
        ///
        /// `io` may be null, in which case pixel frames are parsed, counted and
        /// dropped — which is what a relic with no strips wants.
        void tick(float deltaTime, eio::RelicIO* io);

        /// True while the desk owns the pixels. The relic's own rendering
        /// should be skipped entirely rather than computed and overwritten:
        /// generating a frame nobody will see is the expensive half of a tick.
        bool ownsPixels() const { return streaming; }

        /// Pops one queued command, oldest first. Returns false when there are
        /// none. These are the strings RelicCore::handleCommand already takes.
        bool takeCommand(std::string& outCommand);

        /// How long a takeover survives silence. Long enough to ride out a
        /// missed frame or two, short enough that a dead cable is a stumble
        /// rather than a blackout.
        void setHoldover(float seconds) { holdoverSeconds = seconds; }
        float getHoldover() const { return holdoverSeconds; }

        /// Seconds since the last pixel frame, for a status line.
        float sinceLastFrame() const { return silence; }

        const FrameReader& getReader() const { return reader; }

        /// Pixel frames that named a strip this relic does not have.
        uint32_t framesUnrouted() const { return unrouted; }

        /// What a relic answers a Hello with. Set it to something that names
        /// the sculpture; the desk prints it when it opens the port.
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

        /// Bytes typed by a human, gathered between frames. See collectTyped.
        std::string typed;
    };
}
