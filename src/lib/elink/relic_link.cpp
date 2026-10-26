// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "relic_link.h"

#include "../eio/relic.h"
#include "../eio/hsv_strip.h"

using namespace elink;

namespace
{
    /// Bytes to take in one tick.
    ///
    /// A cap rather than "drain it all" because a desk that has got ahead of us
    /// — or a stuck sender — would otherwise hold the render loop for as long
    /// as it kept talking, and a relic that stops drawing to keep reading is
    /// worse than one that runs a frame behind. 4096 is about four full obelisk
    /// frames, so at 40fps this ceiling sits well above anything the link
    /// actually carries and only bites when something has gone wrong.
    constexpr int MAX_BYTES_PER_TICK = 4096;

    /// A show cannot outrun this, and a runaway sender cannot grow it.
    constexpr size_t MAX_QUEUED_COMMANDS = 8;

    /// Longer than any command the relics take, short enough that garbage
    /// which happens to be printable cannot build into anything.
    constexpr size_t MAX_TYPED_LINE = 64;
}

void RelicLink::tick(float deltaTime, eio::RelicIO* io)
{
    if (!transport)
    {
        return;
    }

    for (int budget = 0; budget < MAX_BYTES_PER_TICK; ++budget)
    {
        const int value = transport->readByte();
        if (value < 0)
        {
            break;
        }

        const uint8_t byte = static_cast<uint8_t>(value);
        const bool wasIdle = reader.isIdle();

        if (reader.push(byte))
        {
            handleFrame(io);
            typed.clear();
        }
        else if (wasIdle)
        {
            collectTyped(byte);
        }
        else
        {
            // Mid-frame. Anything the line buffer had was noise around it.
            typed.clear();
        }
    }

    // Ownership lapses on silence, not on being told. Counted after the read
    // above so a frame that arrived this tick resets it.
    silence += deltaTime;
    if (streaming && silence >= holdoverSeconds)
    {
        streaming = false;
        transport->writeLine("EOSLINK release timeout");
    }
}

void RelicLink::handleFrame(eio::RelicIO* io)
{
    switch (reader.type())
    {
        case FrameType::Pixels:
            applyPixels(io);
            break;

        case FrameType::Command:
            queueCommand(std::string(reinterpret_cast<const char*>(reader.payload()),
                                     reader.payloadSize()));
            break;

        case FrameType::Hello:
            answerHello(io);
            break;

        case FrameType::Reboot:
            // Say so before going, because after this there is no link to say
            // anything on. The relic comes back as a mass-storage device.
            transport->writeLine("EOSLINK bootsel");
            transport->rebootToBootloader();
            break;

        case FrameType::Release:
            // The clean end of a show, rather than waiting out the holdover.
            if (streaming)
            {
                streaming = false;
                transport->writeLine("EOSLINK release asked");
            }
            break;

        default:
            break;
    }
}

void RelicLink::applyPixels(eio::RelicIO* io)
{
    PixelHeader header;
    if (!readPixelHeader(reader.payload(), reader.payloadSize(), header))
    {
        return;
    }

    // The takeover starts on the first pixel frame that parses, and every one
    // after it pushes the holdover out.
    silence = 0.0f;
    if (!streaming)
    {
        streaming = true;
        if (transport)
        {
            transport->writeLine("EOSLINK take");
        }
    }

    if (!io)
    {
        return;
    }

    auto strip = io->strips.find(header.strip);
    if (strip == io->strips.end() || !strip->second)
    {
        ++unrouted;
        return;
    }

    eio::HSVStrip* target = strip->second.get();
    const uint8_t* rgb = reader.payload() + PIXEL_HEADER_SIZE;
    const uint16_t length = target->getLength();

    for (uint16_t i = 0; i < header.count; ++i)
    {
        const uint16_t index = static_cast<uint16_t>(header.start + i);
        if (index >= length)
        {
            // A desk configured for a longer strip than this relic has. Take
            // what fits rather than refusing the frame: a partly-lit sculpture
            // is a far better diagnostic than a dark one.
            break;
        }

        target->setPixelRGB(index, rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
    }
}

void RelicLink::answerHello(eio::RelicIO* io)
{
    if (!transport)
    {
        return;
    }

    // Named strips and their lengths, so a desk can check its config against
    // the sculpture rather than against a comment. The brightness matters more
    // than it looks: it is a current limit, the relic applies it on top of
    // whatever the desk sends, and at HIGH that is 63/255 - so a desk showing a
    // bright picture and a sculpture looking dim is not a fault.
    std::string line = "EOSLINK hello " + identity;

    if (io)
    {
        for (const auto& entry : io->strips)
        {
            if (!entry.second)
            {
                continue;
            }
            line += " strip=" + std::to_string(static_cast<int>(entry.first))
                  + ":" + std::to_string(static_cast<int>(entry.second->getLength()));
        }
        line += " brightness=" + std::to_string(
            static_cast<int>(io->strips.empty() ? 255 : io->strips.begin()->second->getStripBrightness()));
    }

    transport->writeLine(line.c_str());
}

void RelicLink::collectTyped(uint8_t byte)
{
    // Typed text, from a human with a serial monitor open. Only ever bytes the
    // frame reader was idle for, so this cannot eat a frame.
    if (byte == '\n' || byte == '\r')
    {
        if (!typed.empty())
        {
            queueCommand(typed);
            typed.clear();
        }
        return;
    }

    // While a desk is streaming, pixel bytes are the overwhelming majority of
    // what turns up here after a dropped frame, and some of them are printable.
    // A human is not typing mid-show; do not invent commands out of pixels.
    if (streaming)
    {
        typed.clear();
        return;
    }

    if (byte < 0x20 || byte > 0x7E)
    {
        typed.clear();
        return;
    }

    if (typed.size() >= MAX_TYPED_LINE)
    {
        typed.clear();
        return;
    }

    typed.push_back(static_cast<char>(byte));
}

void RelicLink::queueCommand(const std::string& command)
{
    if (commands.size() >= MAX_QUEUED_COMMANDS)
    {
        // Drop the oldest: on a backlog the newest cue is the one that matters,
        // and a queue that grows is one that eventually fires a cue from a
        // minute ago.
        commands.erase(commands.begin());
    }
    commands.push_back(command);
}

bool RelicLink::takeCommand(std::string& outCommand)
{
    if (commands.empty())
    {
        return false;
    }

    outCommand = commands.front();
    commands.erase(commands.begin());
    return true;
}
