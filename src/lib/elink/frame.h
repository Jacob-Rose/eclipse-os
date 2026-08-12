// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stddef.h>
#include <stdint.h>

///
/// elink: the wire between a desk and a relic.
///
/// One format, compiled into both ends. The desktop builds frames with
/// writeFrame and the relic reads them with FrameReader, out of this file, so
/// there is no second definition of the protocol to drift out of step. Same
/// reason the patterns are shared rather than ported.
///
/// Nothing here touches Arduino, allocates, or blocks. It is a byte at a time
/// on the way in and a buffer on the way out, which is what lets the whole
/// parser be tested on a host with no hardware attached — see
/// desktop/python/tests and `eclipse-dmx --link-selftest`.
///

namespace elink
{
    /// 0xEC 0x15 — "ec-lipse", and two bytes rather than one because a single
    /// sentinel occurs in pixel data roughly every 256 bytes.
    static constexpr uint8_t MAGIC_0 = 0xEC;
    static constexpr uint8_t MAGIC_1 = 0x15;

    /// magic, magic, type, length lo, length hi
    static constexpr size_t HEADER_SIZE = 5;
    /// crc lo, crc hi
    static constexpr size_t TRAILER_SIZE = 2;

    /// The obelisk is the biggest thing we carry: 344 pixels at three bytes,
    /// plus the five-byte pixel header below, is 1037. The rest is headroom for
    /// a longer relic without a protocol change.
    static constexpr uint16_t MAX_PAYLOAD = 1400;

    enum class FrameType : uint8_t
    {
        /// A run of pixels. Payload is a PixelHeader then 3 bytes per pixel.
        Pixels = 0x01,

        /// A line of text for RelicCore::handleCommand — the same strings the
        /// serial console already accepts. This is cue mode.
        Command = 0x02,

        /// Empty. The relic answers on its log stream, so a desk can tell a
        /// relic from a widget without lighting anything.
        Hello = 0x03,

        /// Empty. Hands the pixels back immediately rather than waiting for the
        /// stream to time out, so a desk can end a show cleanly.
        Release = 0x04,
    };

    /// The front of a Pixels payload.
    ///
    /// `strip` because a relic may have more than one; `start` and `count`
    /// because a frame does not have to carry the whole strip, which is what
    /// lets a desk repaint one side of the obelisk without sending 1032 bytes.
    struct PixelHeader
    {
        uint8_t strip{0};
        uint16_t start{0};
        uint16_t count{0};
    };
    static constexpr size_t PIXEL_HEADER_SIZE = 5;

    /// CRC16-CCITT (poly 0x1021, init 0xFFFF), over type, length and payload.
    ///
    /// Worth the cost precisely because the failure it catches is silent. A
    /// corrupt pixel frame is not an error anyone sees; it is one wrong-coloured
    /// frame in a show, which reads as a glitch in the look rather than as a
    /// bad cable. Bitwise rather than table-driven: 1032 bytes at 40fps is
    /// around 1.5% of an RP2040, and a 512-byte table is not worth the flash.
    uint16_t crc16(const uint8_t* data, size_t length);

    /// Running form, for feeding a frame through as it arrives.
    uint16_t crc16Update(uint16_t crc, uint8_t byte);
    static constexpr uint16_t CRC16_INIT = 0xFFFF;


    /// Writes one frame into `out`, which must have room for
    /// HEADER_SIZE + length + TRAILER_SIZE. Returns bytes written, or 0 when
    /// the payload is too long or the buffer too small.
    size_t writeFrame(FrameType type,
                      const uint8_t* payload,
                      uint16_t length,
                      uint8_t* out,
                      size_t outCapacity);

    /// Fills the five bytes of a PixelHeader. `out` must have room for
    /// PIXEL_HEADER_SIZE.
    void writePixelHeader(const PixelHeader& header, uint8_t* out);

    /// Reads a PixelHeader off the front of a Pixels payload. Returns false
    /// when the payload is too short for it, or when `count` does not match the
    /// bytes that follow.
    bool readPixelHeader(const uint8_t* payload, uint16_t length, PixelHeader& outHeader);


    /// A frame parser that takes one byte at a time.
    ///
    /// Byte at a time on purpose: the relic reads whatever happens to be in the
    /// UART buffer this tick, which is almost never a whole frame, and a reader
    /// that waits for one would either block the render loop or need its own
    /// thread. This one never waits for anything.
    ///
    /// It also resyncs. A relic can be plugged into a running desk, a cable can
    /// be pulled mid-frame, and the very first bytes it ever sees are usually
    /// the tail of something. Anything that is not a clean frame is discarded a
    /// byte at a time until the magic turns up again, which is the only
    /// behaviour that recovers without a reset.
    class FrameReader
    {
    public:
        /// Feeds one byte. Returns true when a complete, checksum-clean frame
        /// is now readable from type() / payload() / payloadSize().
        ///
        /// The frame stays valid until the next push() that returns true, so a
        /// caller may hold onto it for the rest of its tick.
        bool push(uint8_t byte);

        FrameType type() const { return frameType; }
        const uint8_t* payload() const { return buffer; }
        uint16_t payloadSize() const { return payloadLength; }

        void reset();

        /// True when no frame is part-read: the next byte is either a magic or
        /// a byte we will throw away.
        ///
        /// This is what lets typed text and binary frames share one cable. A
        /// human with a serial monitor open is still the fastest way to poke a
        /// relic, and losing that the moment a desk is supported would be a bad
        /// trade — so RelicLink collects printable bytes seen while the reader
        /// is idle into a line, and a frame in flight discards them.
        bool isIdle() const { return state == State::Magic0; }

        /// Counters, for a relic that wants to say how the link is going. A
        /// rising `rejected` is a wiring or timing problem; a rising
        /// `discarded` on its own is just resync after a reconnect.
        uint32_t framesAccepted() const { return accepted; }
        uint32_t framesRejected() const { return rejected; }
        uint32_t bytesDiscarded() const { return discarded; }

    private:
        enum class State : uint8_t
        {
            Magic0,
            Magic1,
            Type,
            LengthLow,
            LengthHigh,
            Payload,
            CrcLow,
            CrcHigh,
        };

        State state{State::Magic0};
        FrameType frameType{FrameType::Hello};
        uint16_t payloadLength{0};
        uint16_t received{0};
        uint16_t runningCrc{CRC16_INIT};
        uint16_t statedCrc{0};

        uint32_t accepted{0};
        uint32_t rejected{0};
        uint32_t discarded{0};

        uint8_t buffer[MAX_PAYLOAD];
    };
}
