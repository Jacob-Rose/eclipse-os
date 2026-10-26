// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

///
/// MIDI out, for lamps.
///
/// The counterpart to midi_input.h, and deliberately much smaller than it.
/// Nothing here knows what a note is: this end of the cable takes a run of
/// bytes and puts them on a port. Everything that decides *which* bytes - a
/// Launchpad's palette, which pad is the live one, the SysEx that puts the
/// surface into a mode that accepts colours at all - lives in the desk, in
/// Python, where it can be tested without a controller plugged in.
///
/// That split is the whole design. A controller's lamp protocol is a moving
/// target and every make spells it differently; a byte is a byte on all of
/// them. So the executable grows one generic verb (`midi send`) rather than
/// a Launchpad driver, and a second make of controller costs nothing here.
///
/// ### why rawmidi, when input uses the sequencer
///
/// Input has to reach *applications* - Mixxx publishes a sequencer port and
/// owns no hardware, so the sequencer is the only place to meet it. Lamps are
/// the other way round: the thing being lit is a physical box on a USB cable,
/// which is exactly what rawmidi addresses.
///
/// It is also the safer half. Writing to the sequencer means constructing a
/// `snd_seq_event_t`, and this rig loads libasound without its headers (see
/// alsa_seq.h) - so the struct would have to be restated here, by hand, and a
/// mistake in it is memory corruption inside a show binary rather than a
/// message that does not arrive. Rawmidi's write takes a pointer and a length.
/// There is no struct to get wrong.
///

namespace edmx
{
    /// One MIDI output the machine can see.
    struct MidiOutPortInfo
    {
        int index{0};         ///< what to pass to open(), as a string
        std::string name;     ///< "Launchpad X LPX MIDI Out"
        std::string address;  ///< "hw:1,0,1" - also accepted verbatim as a spec
    };

    class MidiOutput
    {
    public:
        MidiOutput() = default;
        ~MidiOutput();

        MidiOutput(const MidiOutput&) = delete;
        MidiOutput& operator=(const MidiOutput&) = delete;

        /// MIDI outputs currently attached, in the order ALSA reports them.
        /// Empty on a machine with no libasound, which is not an error.
        static std::vector<MidiOutPortInfo> enumeratePorts();

        /// Resolves a spec to a port. `spec` is an index ("0"), a full name, a
        /// case-insensitive fragment of one ("launchpad"), a card address
        /// ("hw:1,0,1"), or "auto".
        ///
        /// "auto" takes the only output there is and otherwise refuses, on the
        /// same reasoning as the input side: lighting the wrong device is a
        /// puzzle, and refusing says so immediately. Unlike the input side
        /// there is no fallback to publishing and waiting - a lamp with nobody
        /// listening is not a thing that exists.
        static bool resolvePort(const std::string& spec, MidiOutPortInfo& outPort,
                                std::string& outError);

        /// Opens a port for writing. Safe to call on an already open output;
        /// the previous port is closed first.
        bool open(const std::string& spec, std::string& outError);
        void close();

        bool isOpen() const;

        /// The port being written to, for status output.
        std::string getPortName() const;

        /// One run of bytes, written whole and drained before returning.
        ///
        /// Whole because a MIDI message that half-arrives is not a smaller
        /// message, it is a corrupt one - a SysEx cut in the middle leaves the
        /// receiver waiting for an F7 that is never coming. Drained because
        /// the caller's next act may be to close the port, and an undrained
        /// buffer at close is a message that never happened.
        bool send(const unsigned char* data, size_t length, std::string& outError);

        /// `port="Launchpad X LPX MIDI Out" hw:1,0,1 sent=42`
        std::string describe() const;

    private:
        /// snd_rawmidi_t*, opaque in alsa's own headers too.
        void* handle{nullptr};
        std::string portName;
        std::string portAddress;
        unsigned long long sent{0};

        /// send() and close() can arrive from different threads - the protocol
        /// reader and the shutdown path - and a write racing a close is a write
        /// into a freed handle.
        mutable std::mutex lock;
    };
}
