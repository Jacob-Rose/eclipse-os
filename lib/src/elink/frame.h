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
/// One frame format, compiled into both ends: the desk builds frames with
/// writeFrame(), the relic reads them with FrameReader. Nothing here touches
/// Arduino, allocates or blocks, so the parser runs in tests on a host
/// (`eclipse-dmx --link-selftest`).
///
/// A frame is: MAGIC_0 MAGIC_1 type length-lo length-hi payload crc-lo crc-hi.
///

namespace elink
{
    /// 0xEC 0x15, two bytes so pixel data cannot fake a frame start
    static constexpr uint8_t MAGIC_0 = 0xEC;
    static constexpr uint8_t MAGIC_1 = 0x15;

    /// magic, magic, type, length lo, length hi
    static constexpr size_t HEADER_SIZE = 5;
    /// crc lo, crc hi
    static constexpr size_t TRAILER_SIZE = 2;

    /// The obelisk's 344 pixels plus the pixel header is 1037; the rest is
    /// headroom for a longer relic.
    static constexpr uint16_t MAX_PAYLOAD = 1400;

    enum class FrameType : uint8_t
    {
        /// A run of pixels: a PixelHeader then 3 bytes per pixel.
        Pixels = 0x01,

        /// A line of text for RelicCore::handleCommand, the same strings the
        /// serial console takes. Cue mode.
        Command = 0x02,

        /// Empty. The relic answers on its log stream with its identity.
        Hello = 0x03,

        /// Empty. Hands the pixels back at once instead of after the holdover.
        Release = 0x04,

        /// Empty. Reboots the relic into its USB bootloader for a reflash. It
        /// does not come back until flashed or power cycled, which is why this
        /// is a frame type and not a command string.
        Reboot = 0x05,
    };

    /// The front of a Pixels payload. `start` and `count` let a frame carry
    /// part of a strip.
    struct PixelHeader
    {
        uint8_t strip{0};
        uint16_t start{0};
        uint16_t count{0};
    };
    static constexpr size_t PIXEL_HEADER_SIZE = 5;

    /// CRC16-CCITT (poly 0x1021, init 0xFFFF) over type, length and payload.
    uint16_t crc16(const uint8_t* data, size_t length);

    /// Running form, for a frame fed through as it arrives.
    uint16_t crc16Update(uint16_t crc, uint8_t byte);
    static constexpr uint16_t CRC16_INIT = 0xFFFF;


    /// Writes one frame into `out`, which needs HEADER_SIZE + length +
    /// TRAILER_SIZE bytes. Returns bytes written, or 0 when the payload is
    /// too long or the buffer too small.
    size_t writeFrame(FrameType type,
                      const uint8_t* payload,
                      uint16_t length,
                      uint8_t* out,
                      size_t outCapacity);

    /// Fills the PIXEL_HEADER_SIZE bytes of a PixelHeader into `out`.
    void writePixelHeader(const PixelHeader& header, uint8_t* out);

    /// Reads a PixelHeader off the front of a Pixels payload. False when the
    /// payload is too short or `count` does not match the bytes that follow.
    bool readPixelHeader(const uint8_t* payload, uint16_t length, PixelHeader& outHeader);


    /* @brief A frame parser fed one byte at a time, from whatever the UART
    * had this tick. Never blocks, and resyncs on the magic after garbage,
    * a pulled cable or a mid-frame connect.
    *
    *     while ((byte = transport.readByte()) >= 0)
    *         if (reader.push(byte)) handle(reader.type(), reader.payload(), reader.payloadSize());
    */
    class FrameReader
    {
    public:
        /// Feeds one byte. True when a complete, checksum-clean frame is
        /// readable from type() / payload() / payloadSize(); it stays valid
        /// until the next push() that returns true.
        bool push(uint8_t byte);

        FrameType type() const { return frameType; }
        const uint8_t* payload() const { return buffer; }
        uint16_t payloadSize() const { return payloadLength; }

        void reset();

        /// True when no frame is part-read. RelicLink uses this to let typed
        /// console text share the cable: printable bytes seen while idle are
        /// collected into a line.
        bool isIdle() const { return state == State::Magic0; }

        /// Counters for a status line. Rising `rejected` is a wiring or timing
        /// problem; rising `discarded` alone is resync after a reconnect.
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
