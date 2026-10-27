// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "frame.h"

using namespace elink;

uint16_t elink::crc16Update(uint16_t crc, uint8_t byte)
{
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (int bit = 0; bit < 8; ++bit)
    {
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                             : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

uint16_t elink::crc16(const uint8_t* data, size_t length)
{
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0; i < length; ++i)
    {
        crc = crc16Update(crc, data[i]);
    }
    return crc;
}

size_t elink::writeFrame(FrameType type,
                         const uint8_t* payload,
                         uint16_t length,
                         uint8_t* out,
                         size_t outCapacity)
{
    if (length > MAX_PAYLOAD)
    {
        return 0;
    }

    const size_t total = HEADER_SIZE + length + TRAILER_SIZE;
    if (!out || outCapacity < total)
    {
        return 0;
    }
    if (length > 0 && !payload)
    {
        return 0;
    }

    out[0] = MAGIC_0;
    out[1] = MAGIC_1;
    out[2] = static_cast<uint8_t>(type);
    out[3] = static_cast<uint8_t>(length & 0xFF);
    out[4] = static_cast<uint8_t>((length >> 8) & 0xFF);

    for (uint16_t i = 0; i < length; ++i)
    {
        out[HEADER_SIZE + i] = payload[i];
    }

    // Over type and length as well as payload, so a corrupted length is caught
    // rather than sending the reader off to collect 60000 bytes.
    uint16_t crc = CRC16_INIT;
    for (size_t i = 2; i < HEADER_SIZE + length; ++i)
    {
        crc = crc16Update(crc, out[i]);
    }

    out[HEADER_SIZE + length] = static_cast<uint8_t>(crc & 0xFF);
    out[HEADER_SIZE + length + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    return total;
}

void elink::writePixelHeader(const PixelHeader& header, uint8_t* out)
{
    out[0] = header.strip;
    out[1] = static_cast<uint8_t>(header.start & 0xFF);
    out[2] = static_cast<uint8_t>((header.start >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(header.count & 0xFF);
    out[4] = static_cast<uint8_t>((header.count >> 8) & 0xFF);
}

bool elink::readPixelHeader(const uint8_t* payload, uint16_t length, PixelHeader& outHeader)
{
    if (!payload || length < PIXEL_HEADER_SIZE)
    {
        return false;
    }

    PixelHeader header;
    header.strip = payload[0];
    header.start = static_cast<uint16_t>(payload[1] | (payload[2] << 8));
    header.count = static_cast<uint16_t>(payload[3] | (payload[4] << 8));

    // The count and the bytes behind it have to agree, or a truncated frame
    // that happened to pass its CRC would be read as a short strip and leave
    // the tail of the relic holding the previous frame.
    if (static_cast<uint32_t>(length) - PIXEL_HEADER_SIZE != static_cast<uint32_t>(header.count) * 3u)
    {
        return false;
    }

    outHeader = header;
    return true;
}

void FrameReader::reset()
{
    state = State::Magic0;
    payloadLength = 0;
    received = 0;
    runningCrc = CRC16_INIT;
    statedCrc = 0;
}

bool FrameReader::push(uint8_t byte)
{
    switch (state)
    {
        case State::Magic0:
            if (byte == MAGIC_0)
            {
                state = State::Magic1;
            }
            else
            {
                ++discarded;
            }
            return false;

        case State::Magic1:
            if (byte == MAGIC_1)
            {
                state = State::Type;
                runningCrc = CRC16_INIT;
            }
            else if (byte == MAGIC_0)
            {
                // 0xEC 0xEC: the second one may still be the real start, so
                // stay here rather than throwing both away.
                ++discarded;
            }
            else
            {
                discarded += 2;
                state = State::Magic0;
            }
            return false;

        case State::Type:
            frameType = static_cast<FrameType>(byte);
            runningCrc = crc16Update(runningCrc, byte);
            state = State::LengthLow;
            return false;

        case State::LengthLow:
            payloadLength = byte;
            runningCrc = crc16Update(runningCrc, byte);
            state = State::LengthHigh;
            return false;

        case State::LengthHigh:
            payloadLength = static_cast<uint16_t>(payloadLength | (byte << 8));
            runningCrc = crc16Update(runningCrc, byte);

            if (payloadLength > MAX_PAYLOAD)
            {
                // Almost certainly noise that happened to contain the magic.
                // Waiting for 60000 bytes to arrive would wedge the link for
                // the rest of the show, so give up on it now.
                ++rejected;
                state = State::Magic0;
                return false;
            }

            received = 0;
            state = (payloadLength == 0) ? State::CrcLow : State::Payload;
            return false;

        case State::Payload:
            buffer[received++] = byte;
            runningCrc = crc16Update(runningCrc, byte);
            if (received >= payloadLength)
            {
                state = State::CrcLow;
            }
            return false;

        case State::CrcLow:
            statedCrc = byte;
            state = State::CrcHigh;
            return false;

        case State::CrcHigh:
            statedCrc = static_cast<uint16_t>(statedCrc | (byte << 8));
            state = State::Magic0;

            if (statedCrc != runningCrc)
            {
                ++rejected;
                return false;
            }

            ++accepted;
            return true;
    }

    // Unreachable, but a corrupted state should resync rather than sit there.
    state = State::Magic0;
    return false;
}
