// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/midi_input.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "edmx/beat_clock.h"

#if defined(_WIN32)
#  include <windows.h>
#  include <mmsystem.h>
#else
#  include <cerrno>
#  include <dirent.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

using namespace edmx;

namespace
{
    /// 24 MIDI clock ticks to the quarter note. Fixed by the spec, not a knob.
    constexpr int kTicksPerBeat = 24;

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

        if (allowed.size() == 1)
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
        tickInBeat.store(1);
    }

    clock->markBeat(when, fromNote ? BeatSource::MidiNote : BeatSource::MidiClock);
    beatsSeen.fetch_add(1);
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
        out.push_back(message);
    }

    return out;
}

void MidiInput::handleMessage(unsigned char status, unsigned char data1, unsigned char data2,
                              double when)
{
    // ---- the monitor -----------------------------------------------------
    // Channel messages only. Clock ticks arrive 24 times a beat and would bury
    // the one note you are trying to find; `midi status` counts them instead.
    if (monitoring.load() && status < 0xF0 && status >= 0x80)
    {
        const unsigned int packed = static_cast<unsigned int>(status)
                                  | (static_cast<unsigned int>(data1) << 8)
                                  | (static_cast<unsigned int>(data2) << 16);

        const unsigned long long slot = monitorWrites.load();
        monitorRing[slot % kMonitorSlots].store(packed);
        monitorWrites.store(slot + 1);
    }

    // ---- system realtime -------------------------------------------------
    if (status >= 0xF8)
    {
        switch (status)
        {
            case 0xF8: // timing clock
            {
                clockTicks.fetch_add(1);

                const int tick = tickInBeat.load();
                tickInBeat.store((tick + 1) % kTicksPerBeat);

                const bool driving = followClock.load()
                    && !(lastNoteBeat > 0.0 && (when - lastNoteBeat) < kNoteHoldsOffClock);

                // Tempo off the tick 24 ago, which was one beat ago by
                // definition. Exact after a single beat of history, where
                // averaging beat-to-beat gaps takes tens of beats to settle.
                if (driving && clock != nullptr && tickCount >= kTickWindow)
                {
                    const double beatSeconds = when - tickTimes[tickWrite];
                    if (beatSeconds >= (60.0 / BeatClock::kMaxBpm) &&
                        beatSeconds <= (60.0 / BeatClock::kMinBpm))
                    {
                        clock->setBpm(static_cast<float>(60.0 / beatSeconds),
                                      BeatSource::MidiClock, when);
                    }
                }

                tickTimes[tickWrite] = when;
                tickWrite = (tickWrite + 1) % kTickWindow;
                if (tickCount < kTickWindow)
                {
                    ++tickCount;
                }

                if (!driving || tick != 0)
                {
                    return;
                }
                onBeat(when, false);
                return;
            }

            case 0xFA: // start - the downbeat is here
                tickInBeat.store(0);
                // The stream restarted, so the window is stale. Keeping it
                // would measure one bogus interval across the gap.
                tickCount = 0;
                tickWrite = 0;
                if (clock != nullptr && followClock.load())
                {
                    clock->restart(when, BeatSource::MidiClock);
                }
                return;

            case 0xFB: // continue - resume where the tick counter left off
            case 0xFC: // stop
                // Stop deliberately does nothing. The clock free-runs, so the
                // rig keeps its pulse through a track change instead of going
                // dead between songs.
                return;

            default:
                return; // active sensing, reset, undefined
        }
    }

    const unsigned char kind = status & 0xF0;
    const int channel = (status & 0x0F) + 1;

    // ---- song position pointer ------------------------------------------
    // Fourteen bits of position, counted in sixteenth notes. Four sixteenths to
    // a beat, six ticks to a sixteenth: that is the phase a bare 0xF8 stream
    // cannot tell us.
    if (status == 0xF2)
    {
        const int sixteenths = (static_cast<int>(data2) << 7) | static_cast<int>(data1);
        tickInBeat.store((sixteenths % 4) * 6);
        return;
    }

    // ---- note on ---------------------------------------------------------
    if (kind == 0x90 && data2 > 0)
    {
        if (!followNotes.load())
        {
            return;
        }

        const int wantedChannel = beatChannel.load();
        if (wantedChannel >= 0 && channel != wantedChannel)
        {
            return;
        }

        const int note = static_cast<int>(data1);

        // The tempo, stated rather than measured. Mixxx sends this on the same
        // beat as the beat note, so by the time the beat lands the period is
        // already right - which matters most at exactly the moment it is
        // hardest to measure, the first beat after a track change.
        const int tempoNote = bpmNote.load();
        if (tempoNote >= 0 && note == tempoNote)
        {
            if (clock != nullptr)
            {
                // velocity + 50, the encoding the mapping uses to fit a usable
                // tempo range into MIDI's 0..127. A velocity of 0 means "below
                // the range", not 50bpm, so it is not worth acting on.
                const float bpm = static_cast<float>(data2) + 50.0f;
                if (data2 > 0)
                {
                    clock->setBpm(bpm, BeatSource::MidiNote, when);
                }
            }
            return;
        }

        const int wantedNote = beatNote.load();
        if (wantedNote >= 0 && note != wantedNote)
        {
            return;
        }

        onBeat(when, true);
    }
}

void MidiInput::handleBytes(const unsigned char* data, size_t length, double when)
{
    for (size_t idx = 0; idx < length; ++idx)
    {
        const unsigned char byte = data[idx];

        // Realtime bytes are allowed to appear *inside* another message, so
        // they are dispatched without touching the parser's state.
        if (byte >= 0xF8)
        {
            handleMessage(byte, 0, 0, when);
            continue;
        }

        if (byte >= 0x80)
        {
            const unsigned char kind = byte & 0xF0;

            // System common clears running status; a channel message sets it.
            if (byte >= 0xF0)
            {
                runningStatus = 0;
                messageWanted = (byte == 0xF2) ? 2 : ((byte == 0xF1 || byte == 0xF3) ? 1 : 0);
            }
            else
            {
                runningStatus = byte;
                messageWanted = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
            }

            messageBytes[0] = byte; // status held in slot 0 while we collect
            messageLength = 0;

            if (messageWanted == 0)
            {
                handleMessage(byte, 0, 0, when);
            }
            continue;
        }

        // A data byte with no status before it: running status, if we have one.
        if (messageWanted == 0)
        {
            if (runningStatus == 0)
            {
                continue; // mid-sysex, or a stream we joined late
            }
            messageBytes[0] = runningStatus;
            messageWanted = ((runningStatus & 0xF0) == 0xC0 || (runningStatus & 0xF0) == 0xD0) ? 1 : 2;
            messageLength = 0;
        }

        if (messageLength == 0)
        {
            messageBytes[1] = byte;
            messageLength = 1;
        }
        else
        {
            handleMessage(messageBytes[0], messageBytes[1], byte, when);
            messageLength = 0;
            messageWanted = 0;
            continue;
        }

        if (messageWanted == 1)
        {
            handleMessage(messageBytes[0], messageBytes[1], 0, when);
            messageLength = 0;
            messageWanted = 0;
        }
    }
}

void MidiInput::alignToNow()
{
    tickInBeat.store(0);
    lastNoteBeat = -1.0;
}

std::string MidiInput::describe() const
{
    char text[320];
    std::snprintf(text, sizeof(text),
                  "port=\"%s\" clock=%s notes=%s beat_note=%d bpm_note=%d channel=%d "
                  "ticks=%llu beats=%llu",
                  portName.empty() ? "-" : portName.c_str(),
                  followClock.load() ? "on" : "off",
                  followNotes.load() ? "on" : "off",
                  beatNote.load(), bpmNote.load(), beatChannel.load(),
                  getClockTicks(), getBeats());
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
}

#else

bool MidiInput::open(const std::string& spec, const std::vector<std::string>& ignore,
                     BeatClock* inClock, std::string& outError)
{
    close();

    MidiPortInfo port;
    if (!resolvePort(spec, ignore, port, outError))
    {
        return false;
    }

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

void MidiInput::close()
{
    if (fd < 0)
    {
        opened.store(false);
        return;
    }

    running.store(false);
    opened.store(false);

    // Closing the descriptor is what breaks the blocking read.
    const int closing = fd;
    fd = -1;
    ::close(closing);

    if (reader.joinable())
    {
        reader.join();
    }

    clock = nullptr;
    portName.clear();
}

#endif
