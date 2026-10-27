// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edesk/midi_input.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ebeat/beat_clock.h>
#include <ebeat/level_bus.h>
#include <ebeat/shared.h>


#if defined(_WIN32)
#  include <windows.h>
#  include <mmsystem.h>
#else
#  include <cerrno>
#  include <dirent.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <unistd.h>

#  include "edesk/alsa_seq.h"
#endif

using namespace edesk;
using namespace ebeat;

namespace
{
    /// How long a note-based beat suppresses clock-based ones. A source that
    /// sends both would otherwise fire two beats slightly out of step with each
    /// other, which reads as a stutter. Notes carry the downbeat explicitly, so
    /// notes win, and the suppression lapses if they stop.
    constexpr double kNoteHoldsOffClock = 2.0;

    std::string toLower(const std::string& text)
    {
        std::string out = text;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool isAllDigits(const std::string& text)
    {
        if (text.empty())
        {
            return false;
        }
        return std::all_of(text.begin(), text.end(),
                           [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    /// Names that are almost certainly the thing we want to sync to. Checked in
    /// order, so a port named for the software beats a generic loopback cable
    /// when both are present.
    ///
    /// Deliberately no hardware brand names. A port called "Traktor Kontrol S2"
    /// is a *controller* — buttons and jogs, not tempo — and matching it would
    /// pick the one device in the room guaranteed to be the wrong one.
    const std::vector<std::string>& autoHints()
    {
        static const std::vector<std::string> hints = {
            "mixxx", "light", "loopmidi", "loopbe", "iac", "virtual",
        };
        return hints;
    }

#if !defined(_WIN32)

    /// What this rig calls itself on the ALSA sequencer. Mixxx lists ports by
    /// these two names, so they are what an operator picks out of a menu at
    /// load-in: keep them recognisable and keep them stable.
    const char* kSeqClientName = "eclipse-dmx";
    const char* kSeqPortName   = "eclipse-dmx IN";

    /// Big enough for any channel message with room to spare. Sysex is the only
    /// thing that would want more, and nothing here reads sysex.
    constexpr size_t kSeqDecodeBytes = 64;

    /// A sequencer port worth offering as a tempo source.
    ///
    /// `SUBS_READ` is the whole test: it means "another client may subscribe to
    /// what this port sends". A port that is readable but not subscribable —
    /// the PipeWire bridge's inputs, for one — cannot be a source no matter how
    /// promising its name, and listing it would only be a thing to try and fail
    /// at in the dark.
    bool isUsableSeqSource(unsigned int caps)
    {
        const unsigned int wanted = alsaseq::kCapRead | alsaseq::kCapSubsRead;
        return (caps & wanted) == wanted && (caps & alsaseq::kCapNoExport) == 0;
    }

    /// `Mixxx:Out`, or just `Midi Through Port-0` where the port name already
    /// says which client it belongs to. Doubling the name up reads as a stutter
    /// and makes `midi.port` fragments harder to write, not easier.
    std::string seqPortLabel(const std::string& clientName, const std::string& portName)
    {
        if (clientName.empty())
        {
            return portName;
        }
        if (portName.rfind(clientName, 0) == 0)
        {
            return portName;
        }
        return clientName + ":" + portName;
    }

    /// Every subscribable source the sequencer knows about. False means there
    /// is no sequencer here at all — no libasound, or nothing listening — which
    /// is a different thing from there being one with nothing on it.
    bool enumerateSeqPorts(std::vector<MidiPortInfo>& ports)
    {
        const AlsaSeq* alsa = AlsaSeq::get();
        if (alsa == nullptr)
        {
            return false;
        }

        void* seq = nullptr;
        if (alsa->open(&seq, "default", alsaseq::kOpenInput, 0) < 0 || seq == nullptr)
        {
            return false;
        }
        alsa->setClientName(seq, kSeqClientName);

        // We are a client the moment we opened, so we would otherwise be in our
        // own list.
        const int self = alsa->clientId(seq);

        void* clientInfo = nullptr;
        void* portInfo = nullptr;
        if (alsa->clientInfoMalloc(&clientInfo) >= 0 && alsa->portInfoMalloc(&portInfo) >= 0)
        {
            alsa->clientInfoSetClient(clientInfo, -1); // -1 starts the walk
            while (alsa->queryNextClient(seq, clientInfo) >= 0)
            {
                const int client = alsa->clientInfoGetClient(clientInfo);
                if (client == self || client == alsaseq::kClientSystem)
                {
                    continue;
                }

                const char* rawClientName = alsa->clientInfoGetName(clientInfo);
                const std::string clientName = (rawClientName != nullptr) ? rawClientName : "";

                // Our own published port, from a *running* instance - a
                // different client id than the one this enumeration just
                // opened, so skipping `self` does not cover it. It is readable
                // now (see openAlsaSeq for why) and would otherwise be offered
                // as a tempo source, which it can never be: it is the thing
                // Mixxx sends *to*.
                if (clientName == kSeqClientName)
                {
                    continue;
                }

                alsa->portInfoSetClient(portInfo, client);
                alsa->portInfoSetPort(portInfo, -1);
                while (alsa->queryNextPort(seq, portInfo) >= 0)
                {
                    if (!isUsableSeqSource(alsa->portInfoGetCapability(portInfo)))
                    {
                        continue;
                    }

                    const char* rawPortName = alsa->portInfoGetName(portInfo);

                    MidiPortInfo info;
                    info.index   = static_cast<int>(ports.size());
                    info.backend = MidiBackend::AlsaSeq;
                    info.client  = client;
                    info.port    = alsa->portInfoGetPort(portInfo);
                    info.name    = seqPortLabel(clientName,
                                                (rawPortName != nullptr) ? rawPortName : "");
                    ports.push_back(info);
                }
            }
        }

        if (portInfo != nullptr)
        {
            alsa->portInfoFree(portInfo);
        }
        if (clientInfo != nullptr)
        {
            alsa->clientInfoFree(clientInfo);
        }
        alsa->close(seq);
        return true;
    }

    /// `"128:0"` split into its two numbers. This is how aconnect and every
    /// piece of ALSA documentation names a port, so it is worth accepting in
    /// `midi.port` verbatim — and it is the only way to be unambiguous when two
    /// clients have picked the same name.
    bool parseSeqAddress(const std::string& text, int& outClient, int& outPort)
    {
        const size_t colon = text.find(':');
        if (colon == std::string::npos)
        {
            return false;
        }

        const std::string client = text.substr(0, colon);
        const std::string port   = text.substr(colon + 1);
        if (!isAllDigits(client) || !isAllDigits(port))
        {
            return false;
        }

        outClient = std::stoi(client);
        outPort   = std::stoi(port);
        return true;
    }

#endif
}

std::string MidiPortInfo::address() const
{
    if (backend != MidiBackend::AlsaSeq)
    {
        return std::string();
    }
    return std::to_string(client) + ":" + std::to_string(port);
}

// ============================================================================
// enumeration
// ============================================================================

#if defined(_WIN32)

std::vector<MidiPortInfo> MidiInput::enumeratePorts()
{
    std::vector<MidiPortInfo> ports;

    const UINT count = midiInGetNumDevs();
    for (UINT id = 0; id < count; ++id)
    {
        MIDIINCAPSA caps;
        std::memset(&caps, 0, sizeof(caps));
        if (midiInGetDevCapsA(id, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        {
            continue;
        }

        MidiPortInfo info;
        info.index = static_cast<int>(id);
        info.name = caps.szPname;
        ports.push_back(info);
    }

    return ports;
}

#else

std::vector<MidiPortInfo> MidiInput::enumeratePorts()
{
    std::vector<MidiPortInfo> ports;

    // The sequencer first, and *instead* where it has anything: every rawmidi
    // device also shows up there, so listing both would show every controller
    // twice under two different names and two different indices.
    //
    // A sequencer with no sources on it is a different matter — an unusual
    // machine, but a real one — and there the device nodes are all there is.
    if (enumerateSeqPorts(ports) && !ports.empty())
    {
        return ports;
    }

    // ALSA rawmidi devices, read straight off the device tree. Going through
    // libasound would give nicer names, but it would also make a lighting
    // binary depend on the sound stack being installed, and the device nodes
    // are perfectly readable without it.
    const char* directories[] = {"/dev/snd", "/dev"};
    for (const char* directory : directories)
    {
        DIR* handle = opendir(directory);
        if (handle == nullptr)
        {
            continue;
        }

        std::vector<std::string> names;
        while (const dirent* entry = readdir(handle))
        {
            const std::string name = entry->d_name;
            const bool isRawMidi = (name.rfind("midiC", 0) == 0);
            const bool isLegacy  = (name.rfind("midi", 0) == 0) && !isRawMidi;
            if (isRawMidi || isLegacy)
            {
                names.push_back(name);
            }
        }
        closedir(handle);

        std::sort(names.begin(), names.end());
        for (const std::string& name : names)
        {
            MidiPortInfo info;
            info.index = static_cast<int>(ports.size());
            info.name = std::string(directory) + "/" + name;
            ports.push_back(info);
        }
    }

    return ports;
}

#endif

bool MidiInput::isClockNote(const emidi::Message& message) const
{
    return followNotes.load() && notes.claims(message);
}

bool MidiInput::markPads(const std::string& name, bool wholeDevice)
{
    // The same match the ignore list uses: a fragment of the port's name.
    if (!isIgnored(name, pads))
    {
        return false;
    }
    padsName = name;
    if (wholeDevice)
    {
        allPads.store(true);
    }
    return true;
}

bool MidiInput::isIgnored(const std::string& name, const std::vector<std::string>& ignore)
{
    const std::string lowered = toLower(name);
    for (const std::string& fragment : ignore)
    {
        if (!fragment.empty() && lowered.find(toLower(fragment)) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

bool MidiInput::resolvePort(const std::string& spec, const std::vector<std::string>& ignore,
                            MidiPortInfo& outPort, std::string& outError)
{
    const std::vector<MidiPortInfo> ports = enumeratePorts();

    if (ports.empty())
    {
        outError = "no MIDI input devices found";
        return false;
    }

    const auto listPorts = [&ports, &ignore]() {
        std::string list;
        for (const MidiPortInfo& port : ports)
        {
            list += (list.empty() ? "" : ", ") + std::to_string(port.index) + ":" + port.name;
            if (!port.address().empty())
            {
                list += " [" + port.address() + "]";
            }
            if (isIgnored(port.name, ignore))
            {
                list += " (ignored)";
            }
        }
        return list;
    };

    if (spec.empty() || spec == "auto")
    {
        // Everything auto is allowed to consider. A controller on the ignore
        // list is not a tempo source no matter how few other ports there are:
        // it is a box of buttons, and every button press would land as a beat.
        std::vector<MidiPortInfo> allowed;
        for (const MidiPortInfo& port : ports)
        {
            if (!isIgnored(port.name, ignore))
            {
                allowed.push_back(port);
            }
        }

        if (allowed.empty())
        {
            outError = "midi.port is \"auto\" but every MIDI input is on midi.ignore "
                       "(" + listPorts() + ")";
            return false;
        }

        for (const std::string& hint : autoHints())
        {
            for (const MidiPortInfo& port : allowed)
            {
                if (toLower(port.name).find(hint) != std::string::npos)
                {
                    outPort = port;
                    return true;
                }
            }
        }

        // The only port there is, on the assumption that a machine with one
        // MIDI socket and a rig plugged into it means that socket.
        //
        // Not on the sequencer, though. There the assumption does not hold —
        // every application on the machine is a port, so "the only one" is an
        // accident of what happens to be running — and it does not need to,
        // because open() has somewhere better to fall back to: our own
        // published port, which no controller can send a stray pad hit down.
        if (allowed.size() == 1 && allowed.front().backend == MidiBackend::Native)
        {
            outPort = allowed.front();
            return true;
        }

        // Refuse rather than pick. Opening the wrong input looks exactly like
        // opening none, and the operator would have no reason to suspect it.
        outError = "midi.port is \"auto\" but none of these look like a tempo source; "
                   "name one explicitly (" + listPorts() + ")";
        return false;
    }

    if (isAllDigits(spec))
    {
        const int wanted = std::stoi(spec);
        for (const MidiPortInfo& port : ports)
        {
            if (port.index == wanted)
            {
                outPort = port;
                return true;
            }
        }
        outError = "no MIDI input at index " + spec + " (" + listPorts() + ")";
        return false;
    }

#if !defined(_WIN32)
    // A sequencer address, `client:port`, as aconnect prints it.
    int wantedClient = -1;
    int wantedPort = -1;
    if (parseSeqAddress(spec, wantedClient, wantedPort))
    {
        for (const MidiPortInfo& port : ports)
        {
            if (port.backend == MidiBackend::AlsaSeq
                && port.client == wantedClient && port.port == wantedPort)
            {
                outPort = port;
                return true;
            }
        }
        outError = "no MIDI input at sequencer address " + spec + " (" + listPorts() + ")";
        return false;
    }
#endif

    const std::string needle = toLower(spec);

    for (const MidiPortInfo& port : ports)
    {
        if (toLower(port.name) == needle)
        {
            outPort = port;
            return true;
        }
    }
    for (const MidiPortInfo& port : ports)
    {
        if (toLower(port.name).find(needle) != std::string::npos)
        {
            outPort = port;
            return true;
        }
    }

    outError = "no MIDI input matching '" + spec + "' (" + listPorts() + ")";
    return false;
}

// ============================================================================
// message handling
//
// Platform-independent: both backends funnel into handleMessage().
// ============================================================================

/// The follower reports ticks it has counted; this decides whether the clock
/// is driving (followClock on, and no note-beat within kNoteHoldsOffClock)
/// and forwards to the BeatClock under that rule.
class MidiInput::ClockRelay : public emidi::ClockSink
{
public:
    ClockRelay(MidiInput& inInput, bool inDriving) : input(inInput), driving(inDriving) {}

    void onBeat(double when) override
    {
        if (driving)
        {
            input.onBeat(when, false);
        }
    }

    void onTempo(float bpm, double when) override
    {
        if (driving && input.clock != nullptr)
        {
            input.clock->setBpm(bpm, BeatSource::MidiClock, when);
        }
    }

    void onRestart(double when) override
    {
        if (input.clock != nullptr && input.followClock.load())
        {
            input.clock->restart(when, BeatSource::MidiClock);
        }
    }

private:
    MidiInput& input;
    bool driving;
};

void MidiInput::onBeat(double when, bool fromNote)
{
    if (clock == nullptr)
    {
        return;
    }

    if (fromNote)
    {
        lastNoteBeat = when;
        // A note is a downbeat, so put the tick grid back in step with it. One
        // tick already counts as consumed: the next 0xF8 is tick 1, not 0.
        follower.setTickInBeat(1);
    }

    clock->markBeat(when, fromNote ? BeatSource::MidiNote : BeatSource::MidiClock);
    beatsSeen.fetch_add(1);
}

static_assert(kVuSourceCount == emidi::BeatNotes::kLevelSlots, "a VuSource is a BeatNotes level slot");

void MidiInput::setVuNote(VuSource source, int note)
{
    notes.setLevelNote(static_cast<int>(source), note);
}

int MidiInput::getVuNote(VuSource source) const
{
    return notes.getLevelNote(static_cast<int>(source));
}

void MidiInput::setMonitor(bool enable)
{
    if (enable)
    {
        // Start from now rather than showing whatever was left in the ring
        // from a previous session.
        monitorRead = monitorWrites.load();
    }
    monitoring.store(enable);
}

std::vector<MidiMessage> MidiInput::drainMonitor(unsigned long long& outDropped)
{
    const unsigned long long written = monitorWrites.load();

    outDropped = 0;
    if (written > monitorRead + kMonitorSlots)
    {
        outDropped = written - monitorRead - kMonitorSlots;
        monitorRead = written - kMonitorSlots;
    }

    std::vector<MidiMessage> out;
    out.reserve(static_cast<size_t>(written - monitorRead));

    for (; monitorRead < written; ++monitorRead)
    {
        const unsigned int packed = monitorRing[monitorRead % kMonitorSlots].load();

        MidiMessage message;
        message.status = static_cast<unsigned char>(packed & 0xFF);
        message.data1  = static_cast<unsigned char>((packed >> 8) & 0xFF);
        message.data2  = static_cast<unsigned char>((packed >> 16) & 0xFF);
        message.origin = static_cast<MidiMessage::Origin>((packed >> 24) & 0x03);
        out.push_back(message);
    }

    return out;
}

void MidiInput::handleMessage(unsigned char status, unsigned char data1, unsigned char data2,
                              double when, bool fromPads)
{
    handleMessage(emidi::Message(status, data1, data2), when, fromPads);
}

void MidiInput::handleMessage(const emidi::Message& message, double when, bool fromPads)
{
    // the monitor sees every channel message, pads included, with its origin
    if (monitoring.load() && message.isChannel())
    {
        // with pads named, everything that is not theirs is the beat's, plugged in or not
        const bool pads = fromPads || allPads.load();
        const bool padsOnly = padsClient.load() >= 0 || padsNamed.load();
        unsigned int origin = pads ? 1u : (padsOnly ? 2u : 0u);
        if (origin == 0u && isClockNote(message))
        {
            origin = 2u;
        }
        const unsigned int packed = static_cast<unsigned int>(message.status)
                                  | (static_cast<unsigned int>(message.data1) << 8)
                                  | (static_cast<unsigned int>(message.data2) << 16)
                                  | (origin << 24);
        const unsigned long long slot = monitorWrites.load();
        monitorRing[slot % kMonitorSlots].store(packed);
        monitorWrites.store(slot + 1);
    }

    // a pad is for the desk's map, never for the clock
    if (fromPads || allPads.load())
    {
        return;
    }

    // the 0xF8 stream and song position: the library follows it, the relay
    // applies the desk's rule that a recent note beat holds the clock off
    if (message.isRealtime() || message.status == emidi::status::SongPosition)
    {
        if (alignRequested.exchange(false))
        {
            follower.alignToNow();
        }
        const bool driving = followClock.load()
            && !(lastNoteBeat > 0.0 && (when - lastNoteBeat) < kNoteHoldsOffClock);
        ClockRelay relay(*this, driving);
        follower.setBpmRange(BeatClock::kMinBpm, BeatClock::kMaxBpm);
        follower.handle(message, when, relay);
        clockTicks.store(follower.getTicks());
        return;
    }

    // notes: the beat, the tempo and the levels, by the configured numbering
    if (!followNotes.load())
    {
        return;
    }
    const emidi::BeatNotes::Event event = notes.decode(message);
    switch (event.kind)
    {
        case emidi::BeatNotes::Kind::Tempo:
            if (clock != nullptr)
            {
                clock->setBpm(event.bpm, BeatSource::MidiNote, when);
            }
            return;

        case emidi::BeatNotes::Kind::Level:
            if (audioLevel != nullptr)
            {
                const VuSource source = static_cast<VuSource>(event.level);
                audioLevel->set(source, event.value, when);
                if (source == VuSource::Average && vuFillsLevel.load())
                {
                    audioLevel->set(AudioChannel::Level, event.value, when);
                }
            }
            return;

        case emidi::BeatNotes::Kind::Beat:
            onBeat(when, true);
            return;

        case emidi::BeatNotes::Kind::None:
            return;
    }
}

namespace
{
    /// The parser's messages, into handleMessage with the flags it needs.
    struct IntoHandleMessage : public emidi::MessageSink
    {
        MidiInput& input;
        bool fromPads;
        IntoHandleMessage(MidiInput& inInput, bool inFromPads) : input(inInput), fromPads(inFromPads) {}
        void onMessage(const emidi::Message& message, double when) override
        {
            input.handleMessage(message, when, fromPads);
        }
    };
}

void MidiInput::handleBytes(const unsigned char* data, size_t length, double when,
                            bool fromPads)
{
    IntoHandleMessage sink(*this, fromPads);
    parser.feed(data, length, when, sink);
}

void MidiInput::alignToNow()
{
    alignRequested.store(true);
    lastNoteBeat = -1.0;
}

std::string MidiInput::describe() const
{
    char text[384];
    std::snprintf(text, sizeof(text),
                  "port=\"%s\"%s clock=%s notes=%s beat_note=%d bpm_note=%d "
                  "vu_inst=%d vu_avg=%d vu_meter=%d channel=%d ticks=%llu beats=%llu",
                  portName.empty() ? "-" : portName.c_str(),
                  padsName.empty() ? "" : " pads=yes",
                  followClock.load() ? "on" : "off",
                  followNotes.load() ? "on" : "off",
                  notes.getBeatNote(), notes.getTempoNote(),
                  getVuNote(VuSource::Instant),
                  getVuNote(VuSource::Average),
                  getVuNote(VuSource::Meter),
                  notes.getChannel(), getClockTicks(), getBeats());
    return std::string(text);
}

// ============================================================================
// the device
// ============================================================================

MidiInput::~MidiInput()
{
    close();
}

#if defined(_WIN32)

namespace
{
    /// winmm's callback runs on a system thread with real restrictions on what
    /// it may call. Everything here is a timestamp, a few atomic stores and
    /// arithmetic — no allocation, no locks, no midi* calls.
    void CALLBACK midiCallback(HMIDIIN, UINT message, DWORD_PTR instance,
                               DWORD_PTR param1, DWORD_PTR param2)
    {
        (void)param2;
        if (message != MIM_DATA)
        {
            return; // MIM_LONGDATA is sysex, which carries no tempo
        }

        MidiInput* input = reinterpret_cast<MidiInput*>(instance);
        if (input == nullptr)
        {
            return;
        }

        const unsigned char status = static_cast<unsigned char>(param1 & 0xFF);
        const unsigned char data1  = static_cast<unsigned char>((param1 >> 8) & 0x7F);
        const unsigned char data2  = static_cast<unsigned char>((param1 >> 16) & 0x7F);

        // param2 carries winmm's own millisecond stamp, but it is relative to
        // midiInStart and only millisecond-resolution. Our own clock is on the
        // same timebase as the render loop, which is what actually matters.
        input->handleMessage(status, data1, data2, nowSeconds());
    }
}

bool MidiInput::open(const std::string& spec, const std::vector<std::string>& ignore,
                     BeatClock* inClock, std::string& outError)
{
    close();

    MidiPortInfo port;
    if (!resolvePort(spec, ignore, port, outError))
    {
        return false;
    }

    clock = inClock;
    markPads(port.name, /*wholeDevice*/ true);

    HMIDIIN opened_handle = nullptr;
    const MMRESULT result = midiInOpen(&opened_handle, static_cast<UINT>(port.index),
                                       reinterpret_cast<DWORD_PTR>(&midiCallback),
                                       reinterpret_cast<DWORD_PTR>(this),
                                       CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
        char text[MAXERRORLENGTH];
        if (midiInGetErrorTextA(result, text, sizeof(text)) != MMSYSERR_NOERROR)
        {
            std::snprintf(text, sizeof(text), "error %u", static_cast<unsigned>(result));
        }
        outError = "could not open MIDI input '" + port.name + "': " + text;
        clock = nullptr;
        return false;
    }

    if (midiInStart(opened_handle) != MMSYSERR_NOERROR)
    {
        midiInClose(opened_handle);
        outError = "could not start MIDI input '" + port.name + "'";
        clock = nullptr;
        return false;
    }

    handle = opened_handle;
    portName = port.name;
    opened.store(true);
    return true;
}

void MidiInput::close()
{
    if (handle == nullptr)
    {
        opened.store(false);
        return;
    }

    HMIDIIN opened_handle = static_cast<HMIDIIN>(handle);
    handle = nullptr;
    opened.store(false);

    midiInStop(opened_handle);
    midiInReset(opened_handle); // returns any queued buffers before the close
    midiInClose(opened_handle);

    clock = nullptr;
    portName.clear();
    padsName.clear();
    padsClient.store(-1);
    allPads.store(false);
}

#else

bool MidiInput::open(const std::string& spec, const std::vector<std::string>& ignore,
                     BeatClock* inClock, std::string& outError)
{
    close();

    // A path is a device node, always, and skips resolution entirely. This is
    // the escape hatch for a config that names one: without it, a machine whose
    // sequencer is answering for everything would have no way left to say "no,
    // that one, the actual socket".
    if (!spec.empty() && spec[0] == '/')
    {
        MidiPortInfo port;
        port.name = spec;
        return openRawMidi(port, inClock, outError);
    }

    const bool haveSeq = (AlsaSeq::get() != nullptr);
    const std::string wanted = toLower(spec);

    // Publish and wait. Nothing to resolve, because the source does the
    // connecting: this is Mixxx picking `eclipse-dmx IN` out of its controller
    // list, which is the shortest path there is on this platform.
    if (haveSeq && wanted == "listen")
    {
        return openAlsaSeq(nullptr, inClock, outError);
    }

    MidiPortInfo port;
    std::string resolveError;
    if (!resolvePort(spec, ignore, port, resolveError))
    {
        // "auto" with nothing recognisable on it is not a failure on the
        // sequencer. Elsewhere refusing is right, because the only alternative
        // is guessing at a source and opening the wrong one — which looks
        // exactly like opening none. Here there is a third option and it beats
        // both: publish the port and let the source come to us. Nothing can be
        // wrong about a port nobody is sending to yet.
        //
        // A *named* port that is not there is still an error, on every backend.
        // Naming one means you meant it, and quietly listening instead would
        // turn a typo into a rig that never pulses for no stated reason.
        if (haveSeq && (spec.empty() || wanted == "auto"))
        {
            return openAlsaSeq(nullptr, inClock, outError);
        }
        outError = resolveError;
        return false;
    }

    if (port.backend == MidiBackend::AlsaSeq)
    {
        return openAlsaSeq(&port, inClock, outError);
    }
    markPads(port.name, /*wholeDevice*/ true);
    return openRawMidi(port, inClock, outError);
}

bool MidiInput::openRawMidi(const MidiPortInfo& port, BeatClock* inClock, std::string& outError)
{
    const int opened_fd = ::open(port.name.c_str(), O_RDONLY);
    if (opened_fd < 0)
    {
        outError = "could not open MIDI input '" + port.name + "': " + std::strerror(errno);
        return false;
    }

    clock = inClock;
    fd = opened_fd;
    portName = port.name;
    opened.store(true);
    running.store(true);

    // A thread doing a blocking read, rather than polling the device from the
    // render loop: a beat that arrives 20ms late is a beat that lands on the
    // wrong frame, and the render loop has a DMX frame to get out on time.
    reader = std::thread([this]() {
        unsigned char buffer[64];
        while (running.load())
        {
            const ssize_t got = ::read(fd, buffer, sizeof(buffer));
            if (got > 0)
            {
                handleBytes(buffer, static_cast<size_t>(got), nowSeconds());
            }
            else if (got < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                break; // device went away, or we are shutting down
            }
        }
    });

    return true;
}

bool MidiInput::openAlsaSeq(const MidiPortInfo* source, BeatClock* inClock,
                            std::string& outError)
{
    const AlsaSeq* alsa = AlsaSeq::get();
    if (alsa == nullptr)
    {
        outError = "no ALSA sequencer on this machine (libasound.so.2 did not load)";
        return false;
    }

    void* handle = nullptr;
    if (alsa->open(&handle, "default", alsaseq::kOpenInput, 0) < 0 || handle == nullptr)
    {
        outError = "could not open the ALSA sequencer";
        return false;
    }
    alsa->setClientName(handle, kSeqClientName);

    // Writable and subscribable, which is what makes this a *destination*: any
    // other client may now send to it. That is the whole trick on Linux — there
    // is no virtual cable to install, because this is one.
    //
    // Readable too, and that is not decoration. DJ software builds its
    // controller list by pairing an input with an output of the same name — a
    // controller is a thing you both send to and hear from — so a port that is
    // only writable is an output with no counterpart and Mixxx does not offer
    // it at all. Every virtual cable this replaces is bidirectional for the
    // same reason: loopMIDI's ports are, and so is a macOS IAC bus. Nothing is
    // ever sent out of it; it exists so the port looks like what it is standing
    // in for.
    const int publishedPort = alsa->createSimplePort(
        handle, kSeqPortName,
        alsaseq::kCapWrite | alsaseq::kCapSubsWrite
            | alsaseq::kCapRead | alsaseq::kCapSubsRead,
        alsaseq::kTypeMidiGeneric | alsaseq::kTypeApplication);
    if (publishedPort < 0)
    {
        alsa->close(handle);
        outError = std::string("could not publish '") + kSeqPortName + "' on the ALSA sequencer";
        return false;
    }

    const std::string self = std::to_string(alsa->clientId(handle)) + ":"
                           + std::to_string(publishedPort);

    if (source != nullptr
        && alsa->connectFrom(handle, publishedPort, source->client, source->port) < 0)
    {
        alsa->deleteSimplePort(handle, publishedPort);
        alsa->close(handle);
        outError = "could not subscribe to '" + source->name + "' (" + source->address() + ")";
        return false;
    }

    // The subscribed source is one client among those reaching the published
    // port - Mixxx arrives on it too, unasked - so a pad controller is told
    // apart per event by its client, not by the port everything shares.
    if (source != nullptr && markPads(source->name, /*wholeDevice*/ false))
    {
        padsClient.store(source->client);
    }

    // The sequencer hands over parsed events; this turns them back into the
    // bytes they arrived as, so everything downstream is the same code on every
    // platform. Running status off, so each event decodes to one complete
    // message rather than depending on what came before it.
    void* parser = nullptr;
    if (alsa->midiEventNew(kSeqDecodeBytes, &parser) < 0 || parser == nullptr)
    {
        alsa->deleteSimplePort(handle, publishedPort);
        alsa->close(handle);
        outError = "could not create the ALSA MIDI event parser";
        return false;
    }
    alsa->midiEventNoStatus(parser, 1);

    if (::pipe(wake) != 0)
    {
        alsa->midiEventFree(parser);
        alsa->deleteSimplePort(handle, publishedPort);
        alsa->close(handle);
        wake[0] = -1;
        wake[1] = -1;
        outError = std::string("could not create the MIDI wake pipe: ") + std::strerror(errno);
        return false;
    }

    // The reader waits in poll(), not in the sequencer, so a read that never
    // comes is still a thread we can get back at shutdown.
    alsa->nonblock(handle, 1);

    clock = inClock;
    seq = handle;
    seqPort = publishedPort;
    seqParser = parser;
    // Named for the source when there is one, and for our own published port
    // when there is not - "listening" being a state that stays true once
    // something does connect, which "waiting" would not.
    portName = (source != nullptr)
        ? source->name
        : (std::string(kSeqPortName) + " (" + self + "), listening");
    opened.store(true);
    running.store(true);

    reader = std::thread([this]() { readAlsaSeq(); });
    return true;
}

void MidiInput::readAlsaSeq()
{
    const AlsaSeq* alsa = AlsaSeq::get();

    // The sequencer's descriptors plus our wake pipe on the end. Fetched once:
    // a sequencer connection's descriptors are fixed for its lifetime.
    const int seqCount = alsa->pollDescriptorsCount(seq, POLLIN);
    if (seqCount <= 0)
    {
        return;
    }

    std::vector<pollfd> fds(static_cast<size_t>(seqCount) + 1);
    alsa->pollDescriptors(seq, fds.data(), static_cast<unsigned int>(seqCount), POLLIN);
    fds[static_cast<size_t>(seqCount)].fd     = wake[0];
    fds[static_cast<size_t>(seqCount)].events = POLLIN;

    unsigned char bytes[kSeqDecodeBytes];

    while (running.load())
    {
        const int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), -1);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        // close() writing a byte down the pipe is what gets us out of here.
        if ((fds[static_cast<size_t>(seqCount)].revents & POLLIN) != 0)
        {
            break;
        }

        bool broken = false;
        for (int index = 0; index < seqCount && !broken; ++index)
        {
            broken = (fds[static_cast<size_t>(index)].revents & (POLLERR | POLLNVAL)) != 0;
        }
        if (broken)
        {
            break; // the sequencer went away under us
        }

        while (true)
        {
            void* event = nullptr;
            const int got = alsa->eventInput(seq, &event);
            if (got == -EAGAIN)
            {
                break; // that was the last one queued
            }
            if (got == -ENOSPC)
            {
                // The input buffer overran and events were lost. Nothing to be
                // done about the ones that are gone; the next poll picks up
                // whatever is still there.
                break;
            }
            if (got < 0 || event == nullptr)
            {
                broken = (got < 0 && got != -EINTR);
                break;
            }

            const long length = alsa->midiEventDecode(seqParser, bytes,
                                                      static_cast<long>(sizeof(bytes)), event);
            if (length > 0)
            {
                const int padsFrom = padsClient.load();
                const bool fromPads = padsFrom >= 0
                    && AlsaSeq::eventSourceClient(event) == padsFrom;
                handleBytes(bytes, static_cast<size_t>(length), nowSeconds(), fromPads);
            }
            // A negative length is an event with no wire form - a port
            // subscription notice, say. Not ours, and not a problem.
        }

        if (broken)
        {
            break;
        }
    }
}

void MidiInput::close()
{
    if (fd < 0 && seq == nullptr)
    {
        opened.store(false);
        return;
    }

    running.store(false);
    opened.store(false);

    if (seq != nullptr)
    {
        // One byte, and the reader is out of poll(). Closing descriptors out
        // from under alsa would work too and is not something it is promised.
        if (wake[1] >= 0)
        {
            const unsigned char byte = 0;
            const ssize_t wrote = ::write(wake[1], &byte, 1);
            (void)wrote;
        }
    }
    else
    {
        // Closing the descriptor is what breaks the blocking read.
        const int closing = fd;
        fd = -1;
        ::close(closing);
    }

    if (reader.joinable())
    {
        reader.join();
    }

    if (seq != nullptr)
    {
        if (const AlsaSeq* alsa = AlsaSeq::get())
        {
            if (seqParser != nullptr)
            {
                alsa->midiEventFree(seqParser);
            }
            if (seqPort >= 0)
            {
                alsa->deleteSimplePort(seq, seqPort);
            }
            alsa->close(seq);
        }
        seq = nullptr;
        seqParser = nullptr;
        seqPort = -1;
    }

    for (int& end : wake)
    {
        if (end >= 0)
        {
            ::close(end);
            end = -1;
        }
    }

    clock = nullptr;
    portName.clear();
    padsName.clear();
    padsClient.store(-1);
    allPads.store(false);
}

#endif
