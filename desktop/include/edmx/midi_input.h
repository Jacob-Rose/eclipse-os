// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include "edmx/beat_clock.h"

///
/// MIDI in, for tempo.
///
/// The only thing we want off a MIDI cable is *when the beat is*. Sources say
/// it in one of two ways, and we take both:
///
///   - **A note per beat** — a note-on on each beat, and often a second note
///     carrying the tempo as a number. This is what Mixxx sends, and it is the
///     path this rig actually runs on.
///   - **MIDI beat clock** — a 0xF8 byte 24 times per quarter note, with
///     0xFA/0xFB/0xFC to start, continue and stop. Tempo is exact, because it
///     is measured over 24 ticks rather than guessed. Mixxx does *not* send
///     this; plenty of other things do.
///
/// ### What Mixxx sends
///
/// Its MIDI-for-light mapping (Preferences > Controllers, load "MIDI for light"
/// against a virtual cable) sends note-ons on one channel, default 1:
///
/// | note | dec | meaning                                   |
/// | ---- | --- | ----------------------------------------- |
/// | 0x30 | 48  | deck change                               |
/// | 0x32 | 50  | **the beat**, velocity 100, then note-off  |
/// | 0x34 | 52  | **the tempo**, velocity = bpm - 50         |
/// | 0x40+| 64+ | VU meters, many per second                 |
///
/// Two things follow from that table, and both are why `beatNote` defaults to
/// 50 rather than "any note". The VU notes arrive constantly, so a rig that
/// took any note-on as a beat would strobe rather than pulse. And the tempo
/// arrives *explicitly*, which is better than any interval we could measure —
/// so note 52 is decoded rather than ignored.
///
/// ### Phase
///
/// Beat clock gives tempo but not, on its own, *phase*: a 0xF8 stream joined
/// halfway through a bar has no marker saying which tick is the beat. Start,
/// Continue and Song Position Pointer are what resolve that, and we honour all
/// three. If a source sends none of them, `midi align` (or a tap) puts the
/// downbeat where you say it is. A beat *note* has no such problem — it is the
/// downbeat.
///
/// ### the three ways in
///
/// Windows uses winmm, which hands over messages already framed.
///
/// Linux has two, because Linux has two MIDI worlds. **ALSA rawmidi** is a
/// device node — `/dev/snd/midiC1D0` — and reading it gives the raw byte
/// stream, which is why there is a small parser here for running status and
/// interleaved realtime bytes. **The ALSA sequencer** is the patchbay where
/// *applications* publish ports, and it is where Mixxx lives: it owns no
/// hardware, so it has no device node to read. Software talks to software
/// there, or not at all.
///
/// So on Linux this publishes a sequencer port of its own — `eclipse-dmx IN` —
/// and Mixxx is pointed straight at it. There is no virtual cable to install,
/// because on this platform *we are the cable*. See edmx/alsa_seq.h for how
/// libasound gets loaded without becoming a build dependency, and the readme
/// for the two-minute version of the setup.
///

namespace edmx
{
    /// Which world a port belongs to. They are opened in completely different
    /// ways, so a resolved port has to carry its own.
    enum class MidiBackend
    {
        /// winmm on Windows; an ALSA rawmidi device node on Linux.
        Native,
        /// An ALSA sequencer port, addressed as client:port and *subscribed*
        /// to rather than opened.
        AlsaSeq,
    };

    struct MidiPortInfo
    {
        int index{0};     ///< what to pass to open(), as a string
        std::string name; ///< the device name the OS reports
        MidiBackend backend{MidiBackend::Native};

        /// AlsaSeq only: the address to subscribe from, as `client:port`. Also
        /// accepted verbatim in `midi.port`, for when two ports share a name.
        int client{-1};
        int port{-1};

        /// `"24:0"`, or empty for anything not on the sequencer.
        std::string address() const;
    };

    /// One channel message, as the monitor reports it.
    struct MidiMessage
    {
        unsigned char status{0};
        unsigned char data1{0};
        unsigned char data2{0};

        int channel() const { return (status & 0x0F) + 1; }
        int kind() const { return status & 0xF0; }
    };

    class MidiInput
    {
    public:
        MidiInput() = default;
        ~MidiInput();

        MidiInput(const MidiInput&) = delete;
        MidiInput& operator=(const MidiInput&) = delete;

        /// MIDI inputs currently attached, in the OS's own order.
        static std::vector<MidiPortInfo> enumeratePorts();

        /// True if `name` contains any of `ignore`, case-insensitively.
        static bool isIgnored(const std::string& name, const std::vector<std::string>& ignore);

        /// Resolves a port spec to a device. `spec` is an index ("0"), a full
        /// name, a case-insensitive fragment of one ("mixxx"), a sequencer
        /// address ("128:0", Linux), or "auto".
        ///
        /// "auto" prefers a port whose name looks like DJ software or a virtual
        /// cable, and otherwise takes the only *device* there is. It
        /// deliberately refuses to guess between several unrecognised ones:
        /// opening the wrong input gives a rig that ignores the music for no
        /// visible reason.
        ///
        /// Sequencer ports are never taken as "the only one", because there
        /// every application on the machine is a port and the only one is an
        /// accident of what is running. Nor does the refusal end things there —
        /// see `open()`, which listens instead.
        ///
        /// `ignore` holds name fragments "auto" must never choose. This is for
        /// the DJ controller sitting on the same machine: it is a MIDI input,
        /// it is often the *only* MIDI input, and every pad on it sends notes —
        /// so it is exactly what a naive "auto" would grab and exactly what
        /// must not be allowed to move the beat. Naming a port explicitly
        /// overrides the list, because naming it means you meant it.
        static bool resolvePort(const std::string& spec, const std::vector<std::string>& ignore,
                                MidiPortInfo& outPort, std::string& outError);

        /// Opens a port and starts feeding `clock`. Safe to call on an already
        /// open input; the previous port is closed first.
        ///
        /// On Linux, anything that resolves to the sequencer publishes
        /// `eclipse-dmx IN` first and then subscribes it to the source. Two
        /// consequences worth knowing:
        ///
        ///   - `spec` of **"listen"** publishes the port and connects it to
        ///     nothing. Mixxx does the connecting, by picking `eclipse-dmx IN`
        ///     as its controller output.
        ///   - **"auto"** falls back to that rather than failing. A port
        ///     nobody is sending to is harmless; guessing at a source and
        ///     opening the wrong one is not, which is the whole reason "auto"
        ///     refuses to guess elsewhere.
        ///
        /// Naming a source still connects to it, and a rawmidi device node
        /// ("/dev/snd/midiC1D0") is always opened as one, sequencer or no.
        bool open(const std::string& spec, const std::vector<std::string>& ignore,
                  BeatClock* clock, std::string& outError);
        void close();

        bool isOpen() const { return opened.load(); }

        /// The source, for status output — or a note that we are published and
        /// waiting, when nothing is connected yet.
        const std::string& getPortName() const { return portName; }

        /// Where beats go. open() sets this; the self-test sets it directly so
        /// the message handling can be exercised with no device present.
        void setBeatClock(BeatClock* inClock) { clock = inClock; }

        /// Where VU readings go. Optional — leave it null and they are parsed
        /// and dropped.
        void setAudioLevel(AudioLevel* inLevel) { audioLevel = inLevel; }

        /// Follow 0xF8 beat clock. On by default.
        void setFollowClock(bool enable) { followClock.store(enable); }
        /// Treat note-on as a beat. On by default.
        void setFollowNotes(bool enable) { followNotes.store(enable); }

        /// Which note number is a beat. -1 takes any note-on, which is only
        /// safe on a source that sends nothing else — see the table above.
        void setBeatNote(int note) { beatNote.store(note); }

        /// Which note number carries the tempo, as velocity + 50. -1 to ignore
        /// it and infer the tempo from the gaps between beats instead.
        void setBpmNote(int note) { bpmNote.store(note); }

        /// Which note carries which loudness signal, as velocity 0..127.
        /// -1 on any of them ignores that one.
        ///
        /// All three are read at once and kept separately, because they behave
        /// differently and a look should be able to pick: 64 instantaneous,
        /// 68 the two-second average, 69 a meter bar. See VuSource.
        void setVuNote(VuSource source, int note);
        int getVuNote(VuSource source) const;

        /// Only take beats from this channel (1..16), or -1 for any.
        void setBeatChannel(int channel) { beatChannel.store(channel); }

        /// Declare that now is a downbeat: realigns the clock-tick counter so
        /// the next beat lands here. For a source that sends 0xF8 and nothing
        /// else, this is how the phase gets set.
        void alignToNow();

        /// Report every channel message that arrives, so a mapping can be
        /// identified rather than assumed. This is how you find out what your
        /// DJ software actually sends, which is not always what its wiki says.
        void setMonitor(bool enable);
        bool isMonitoring() const { return monitoring.load(); }

        /// Messages seen since the last drain, oldest first. `outDropped` counts
        /// any that were overwritten before we got to them, which a chatty
        /// mapping will cause and which is worth saying out loud.
        std::vector<MidiMessage> drainMonitor(unsigned long long& outDropped);

        unsigned long long getClockTicks() const { return clockTicks.load(); }
        unsigned long long getBeats() const { return beatsSeen.load(); }

        /// `port="loopMIDI Port" clock=on notes=on ticks=4821 beats=200`
        std::string describe() const;

        /// One already-framed message. Public so the Windows callback, the
        /// byte-stream parser and a test can all reach the same logic.
        void handleMessage(unsigned char status, unsigned char data1, unsigned char data2, double when);

        /// A run of raw bytes, with running status and realtime bytes possibly
        /// interleaved mid-message.
        void handleBytes(const unsigned char* data, size_t length, double when);

    private:
        void onBeat(double when, bool fromNote);

#if !defined(_WIN32)
        /// A device node, read as a byte stream.
        bool openRawMidi(const MidiPortInfo& port, BeatClock* clock, std::string& outError);

        /// Publishes `eclipse-dmx IN` on the sequencer, and subscribes it to
        /// `source` when there is one. Null means publish and wait.
        bool openAlsaSeq(const MidiPortInfo* source, BeatClock* clock,
                         std::string& outError);

        /// The sequencer's reader thread: poll, decode back to bytes, feed
        /// handleBytes().
        void readAlsaSeq();
#endif

        BeatClock* clock{nullptr};
        AudioLevel* audioLevel{nullptr};
        std::string portName;
        std::atomic<bool> opened{false};

        std::atomic<bool> followClock{true};
        std::atomic<bool> followNotes{true};

        // Mixxx's numbering, because that is what this rig is driven by. Both
        // are settable for anything that is not Mixxx.
        std::atomic<int> beatNote{50};
        std::atomic<int> bpmNote{52};
        std::atomic<int> beatChannel{-1};

        /// Indexed by VuSource: instantaneous, two-second average, meter bar.
        std::atomic<int> vuNotes[kVuSourceCount] = {{64}, {68}, {69}};

        std::atomic<unsigned long long> clockTicks{0};
        std::atomic<unsigned long long> beatsSeen{0};

        /// 0..23 within the quarter note. Reset by Start and by alignToNow().
        std::atomic<int> tickInBeat{0};

        /// When each of the last 24 clock ticks arrived.
        ///
        /// A rolling window of exactly one beat, which makes the tempo a
        /// subtraction rather than an average: the tick 24 ago was, by
        /// definition, one beat ago. Measuring only at beat boundaries and
        /// smoothing instead takes tens of beats to settle and is a whole bpm
        /// out for the first several — long enough to see on a rig.
        ///
        /// MIDI-thread only, so no atomics.
        static constexpr int kTickWindow = 24;
        double tickTimes[kTickWindow]{};
        int tickCount{0};
        int tickWrite{0};

        /// When the last note-driven beat landed. Only ever touched from the
        /// MIDI thread, so it does not need to be atomic. See the note about
        /// notes taking precedence over clock in midi_input.cpp.
        double lastNoteBeat{-1.0};

        // -- the monitor ----------------------------------------------------
        //
        // A ring the MIDI thread writes and the render thread drains. A ring
        // rather than a queue because the writer is a device callback: it must
        // not allocate, must not lock, and must not care whether anyone is
        // reading. Overrunning a slow reader loses old messages, which for a
        // diagnostic is the right trade - and the drop count is reported.
        static constexpr size_t kMonitorSlots = 128;

        std::atomic<bool> monitoring{false};
        std::atomic<unsigned int> monitorRing[kMonitorSlots];
        std::atomic<unsigned long long> monitorWrites{0};
        /// Only the draining thread touches this.
        unsigned long long monitorRead{0};

        // -- byte-stream parser state (POSIX path only) ---------------------
        unsigned char runningStatus{0};
        unsigned char messageBytes[2]{0, 0};
        int messageLength{0};
        int messageWanted{0};

#if defined(_WIN32)
        /// HMIDIIN, kept as void* so windows.h stays out of this header.
        void* handle{nullptr};
#else
        /// The rawmidi device node, when that is what we are on.
        int fd{-1};

        // -- the ALSA sequencer, when that is what we are on ------------------
        //
        // snd_seq_t and snd_midi_event_t, both opaque in alsa's headers too.
        void* seq{nullptr};
        void* seqParser{nullptr};
        int seqPort{-1};

        /// Read end, write end. The reader parks in poll() until a message
        /// arrives, and close() has to be able to get it back — a byte down
        /// this pipe is what does it. Closing the descriptor works for a
        /// rawmidi read; a sequencer handle has several descriptors and closing
        /// them under alsa is not something it expects.
        int wake[2]{-1, -1};

        std::thread reader;
        std::atomic<bool> running{false};
#endif
    };
}
