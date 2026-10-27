// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "parser.h"

using namespace emidi;

namespace
{
    int dataBytesFor(uint8_t statusByte)
    {
        if (statusByte >= 0xF0)
        {
            switch (statusByte)
            {
                case status::SongPosition: return 2;
                case status::TimeCode:
                case status::SongSelect:   return 1;
                default:                   return 0;
            }
        }
        const uint8_t kind = statusByte & 0xF0;
        return (kind == status::ProgramChange || kind == status::ChannelAftertouch) ? 1 : 2;
    }
}

void Parser::reset()
{
    runningStatus = 0;
    collected = 0;
    wanted = 0;
}

void Parser::feed(const uint8_t* data, size_t length, double when, MessageSink& sink)
{
    for (size_t idx = 0; idx < length; ++idx)
    {
        feed(data[idx], when, sink);
    }
}

void Parser::feed(uint8_t byte, double when, MessageSink& sink)
{
    // realtime bytes may land inside another message and do not touch its state
    if (byte >= 0xF8)
    {
        sink.onMessage(Message(byte), when);
        return;
    }

    if (byte >= 0x80)
    {
        // system common clears running status; a channel message sets it
        if (byte >= 0xF0)
        {
            runningStatus = 0;
        }
        else
        {
            runningStatus = byte;
        }

        pending[0] = byte;
        collected = 0;
        wanted = dataBytesFor(byte);

        if (wanted == 0)
        {
            // sysex start/end and the data-less system messages: sysex data
            // bytes that follow have no status and fall through below
            if (byte != status::SysexStart && byte != status::SysexEnd)
            {
                sink.onMessage(Message(byte), when);
            }
        }
        return;
    }

    // a data byte with no message in progress: running status, if any
    if (wanted == 0)
    {
        if (runningStatus == 0)
        {
            return; // mid-sysex, or a stream joined late
        }
        pending[0] = runningStatus;
        collected = 0;
        wanted = dataBytesFor(runningStatus);
    }

    if (collected == 0 && wanted == 2)
    {
        pending[1] = byte;
        collected = 1;
        return;
    }

    if (wanted == 1)
    {
        sink.onMessage(Message(pending[0], byte, 0), when);
    }
    else
    {
        sink.onMessage(Message(pending[0], pending[1], byte), when);
    }
    collected = 0;
    wanted = 0;
}
