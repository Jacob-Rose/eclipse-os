// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>

///
/// emidi: MIDI for the library. A message model, a byte-stream parser, a
/// beat-clock follower and a binding from messages to a look's properties and
/// triggers. Transports - a UART, USB, a desk's MIDI API - live with their
/// platform and feed bytes in; everything here builds on both.
///

namespace emidi
{
    /// Status bytes. Channel messages carry the channel in the low nibble.
    namespace status
    {
        constexpr uint8_t NoteOff          = 0x80;
        constexpr uint8_t NoteOn           = 0x90;
        constexpr uint8_t PolyAftertouch   = 0xA0;
        constexpr uint8_t ControlChange    = 0xB0;
        constexpr uint8_t ProgramChange    = 0xC0;
        constexpr uint8_t ChannelAftertouch = 0xD0;
        constexpr uint8_t PitchBend        = 0xE0;

        constexpr uint8_t SysexStart       = 0xF0;
        constexpr uint8_t TimeCode         = 0xF1;
        constexpr uint8_t SongPosition     = 0xF2;
        constexpr uint8_t SongSelect       = 0xF3;
        constexpr uint8_t TuneRequest      = 0xF6;
        constexpr uint8_t SysexEnd         = 0xF7;

        constexpr uint8_t Clock            = 0xF8;
        constexpr uint8_t Start            = 0xFA;
        constexpr uint8_t Continue         = 0xFB;
        constexpr uint8_t Stop             = 0xFC;
        constexpr uint8_t ActiveSensing    = 0xFE;
        constexpr uint8_t Reset            = 0xFF;
    }

    /// Clock ticks per quarter note, fixed by the spec.
    constexpr int kClocksPerBeat = 24;

    /* @brief One MIDI message: a status byte and up to two data bytes.
    *
    * Realtime and system messages have no channel; kind() is the status with
    * the channel nibble cleared, so `kind() == status::NoteOn` reads the same
    * on any channel.
    */
    struct Message
    {
        uint8_t status{0};
        uint8_t data1{0};
        uint8_t data2{0};

        Message() = default;
        Message(uint8_t inStatus, uint8_t inData1 = 0, uint8_t inData2 = 0)
            : status(inStatus), data1(inData1), data2(inData2) {}

        bool isChannel() const { return status >= 0x80 && status < 0xF0; }
        bool isRealtime() const { return status >= 0xF8; }
        bool isSystem() const { return status >= 0xF0; }

        /// The status without its channel, for a channel message; the status
        /// itself otherwise.
        uint8_t kind() const { return isChannel() ? static_cast<uint8_t>(status & 0xF0) : status; }

        /// 1..16 for a channel message, 0 otherwise.
        int channel() const { return isChannel() ? (status & 0x0F) + 1 : 0; }

        /// Note on with a velocity; a velocity of zero is a note off by convention.
        bool isNoteOn() const { return kind() == status::NoteOn && data2 > 0; }
        bool isNoteOff() const { return kind() == status::NoteOff || (kind() == status::NoteOn && data2 == 0); }
        bool isControlChange() const { return kind() == status::ControlChange; }

        /// Note number or controller number, whichever this message carries.
        uint8_t number() const { return data1; }
        /// Velocity or controller value.
        uint8_t value() const { return data2; }

        /// Pitch bend as 0..16383, 8192 centred.
        uint16_t pitchBend() const { return static_cast<uint16_t>((data2 << 7) | data1); }

        /// Song position in sixteenth notes.
        uint16_t songPosition() const { return static_cast<uint16_t>((data2 << 7) | data1); }
    };

    /* @brief Where parsed messages go. `when` is the caller's clock, in
    * seconds, so a beat can be placed inside a frame.
    */
    class MessageSink
    {
    public:
        virtual ~MessageSink() = default;

        virtual void onMessage(const Message& message, double when) = 0;
    };
}
