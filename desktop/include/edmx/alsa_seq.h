// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#if !defined(_WIN32)

#include <cstddef>
#include <poll.h>

///
/// ALSA's sequencer, loaded at runtime instead of linked.
///
/// ### why this file exists
///
/// On Linux there are two MIDI worlds, and DJ software is only in one of them.
///
///   - **rawmidi** — `/dev/snd/midiC1D0`, a byte stream off a physical socket.
///     This is what a USB controller is, and reading it needs nothing but
///     `open()` and `read()`.
///   - **the sequencer** — an in-kernel patchbay where *applications* publish
///     ports and subscribe to each other's. Mixxx is here, and so is anything
///     else that speaks MIDI without owning a piece of hardware.
///
/// A rig that only reads device nodes sees every controller in the room and not
/// the one program actually playing the music. So the sequencer has to be
/// spoken to, and speaking to it means libasound.
///
/// ### why it is dlopen'd
///
/// Linking `-lasound` would put a hard dependency on the sound stack into a
/// binary whose entire job is pushing bytes at a DMX widget — it would need the
/// dev package to build and the runtime to start, on machines that may have
/// neither. Loading it by hand keeps that optional: no libasound, no sequencer,
/// and the rawmidi path is still there. The cost is this table.
///
/// Every handle here is `void*`. The real types are opaque in alsa's own
/// headers too, so nothing is lost by not having them, and it means this
/// compiles on a machine with no `alsa/seq.h` anywhere on it.
///

namespace edmx
{
    /// The subset of alsa-lib's sequencer API this rig uses, as function
    /// pointers. Named after the alsa symbols with the prefix dropped.
    struct AlsaSeq
    {
        // -- the connection -------------------------------------------------
        int  (*open)(void** handle, const char* name, int streams, int mode);
        int  (*close)(void* handle);
        int  (*setClientName)(void* handle, const char* name);
        int  (*clientId)(void* handle);
        int  (*nonblock)(void* handle, int enable);

        // -- our port, and what it is subscribed to --------------------------
        int  (*createSimplePort)(void* handle, const char* name,
                                 unsigned int caps, unsigned int type);
        int  (*deleteSimplePort)(void* handle, int port);
        int  (*connectFrom)(void* handle, int myPort, int srcClient, int srcPort);

        // -- reading ---------------------------------------------------------
        int  (*pollDescriptorsCount)(void* handle, short events);
        int  (*pollDescriptors)(void* handle, struct pollfd* fds,
                                unsigned int space, short events);
        int  (*eventInput)(void* handle, void** event);

        // -- enumeration -----------------------------------------------------
        int  (*clientInfoMalloc)(void** info);
        void (*clientInfoFree)(void* info);
        void (*clientInfoSetClient)(void* info, int client);
        int  (*clientInfoGetClient)(const void* info);
        const char* (*clientInfoGetName)(void* info);
        int  (*queryNextClient)(void* handle, void* info);

        int  (*portInfoMalloc)(void** info);
        void (*portInfoFree)(void* info);
        void (*portInfoSetClient)(void* info, int client);
        void (*portInfoSetPort)(void* info, int port);
        int  (*portInfoGetPort)(const void* info);
        const char* (*portInfoGetName)(const void* info);
        unsigned int (*portInfoGetCapability)(const void* info);
        int  (*queryNextPort)(void* handle, void* info);

        // -- events back into bytes -------------------------------------------
        //
        // The sequencer delivers parsed events, not the wire format. Turning
        // them back into bytes costs nothing and means the note handling here
        // is the *same* code on every platform — one parser, one set of bugs,
        // and `--midi-selftest` exercises the path Linux actually runs on.
        int  (*midiEventNew)(size_t bufferSize, void** parser);
        void (*midiEventFree)(void* parser);
        void (*midiEventNoStatus)(void* parser, int enable);
        long (*midiEventDecode)(void* parser, unsigned char* buffer, long count,
                                const void* event);

        /// The library, or null on a machine without it — which is not an
        /// error, just a machine with no sequencer to talk to. Loaded once and
        /// never unloaded: a reader thread may still be inside it.
        static const AlsaSeq* get();
    };

    /// The rawmidi half of alsa-lib, loaded the same way and out of the same
    /// library handle.
    ///
    /// Separate from AlsaSeq because it is a separate API answering a separate
    /// question: the sequencer is where *applications* meet, rawmidi is where
    /// the hardware is. Output uses this one - see midi_output.h for why - and
    /// a machine can perfectly well have one working and not the other, so
    /// they are refused independently rather than as one table.
    struct AlsaRawMidi
    {
        // -- the connection -------------------------------------------------
        //
        // `in` and `out` are both out-parameters and either may be null: this
        // rig only ever asks for the output half.
        int  (*open)(void** in, void** out, const char* name, int mode);
        int  (*close)(void* handle);
        long (*write)(void* handle, const void* buffer, size_t size);
        int  (*drain)(void* handle);
        const char* (*strerror)(int code);

        // -- enumeration -----------------------------------------------------
        //
        // Walked card by card, then device by device, then subdevice by
        // subdevice - which is how a Launchpad ends up as two ports on one
        // device, its DAW port and its MIDI port, distinguishable only by
        // subdevice name.
        int  (*cardNext)(int* card);
        int  (*ctlOpen)(void** ctl, const char* name, int mode);
        int  (*ctlClose)(void* ctl);
        int  (*ctlRawMidiNextDevice)(void* ctl, int* device);
        int  (*ctlRawMidiInfo)(void* ctl, void* info);

        int  (*infoMalloc)(void** info);
        void (*infoFree)(void* info);
        void (*infoSetDevice)(void* info, unsigned int device);
        void (*infoSetSubdevice)(void* info, unsigned int subdevice);
        void (*infoSetStream)(void* info, int stream);
        unsigned int (*infoGetSubdevicesCount)(const void* info);
        const char* (*infoGetName)(const void* info);
        const char* (*infoGetSubdeviceName)(const void* info);

        /// The library, or null on a machine without it.
        static const AlsaRawMidi* get();
    };

    namespace alsarawmidi
    {
        /// snd_rawmidi_stream_t. ABI, like the sequencer's constants.
        constexpr int kStreamOutput = 1;

        /// Blocking, which is what we want: a lamp repaint is a handful of
        /// bytes and the caller is not in the render loop.
        constexpr int kModeBlocking = 0;
    }

    // alsa's own constants, restated so this compiles with no alsa headers
    // installed. They are ABI, not preference: they travel over the kernel
    // interface and cannot change.
    namespace alsaseq
    {
        constexpr int kOpenInput = 2;

        constexpr unsigned int kCapRead      = 1u << 0;
        constexpr unsigned int kCapWrite     = 1u << 1;
        constexpr unsigned int kCapSubsRead  = 1u << 5;
        constexpr unsigned int kCapSubsWrite = 1u << 6;
        constexpr unsigned int kCapNoExport  = 1u << 7;

        constexpr unsigned int kTypeMidiGeneric = 1u << 1;
        constexpr unsigned int kTypeApplication = 1u << 20;

        /// Client 0 is the kernel's own — Timer and Announce. Subscribable, and
        /// never a tempo source.
        constexpr int kClientSystem = 0;
    }
}

#endif // !_WIN32
