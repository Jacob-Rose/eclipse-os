// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/midi_output.h"

#include <algorithm>
#include <cctype>

#if !defined(_WIN32)
#include "edmx/alsa_seq.h"
#endif

using namespace edmx;

namespace
{
    std::string lowered(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    bool contains(const std::string& haystack, const std::string& needle)
    {
        return lowered(haystack).find(lowered(needle)) != std::string::npos;
    }

    /// True for a spec that is nothing but digits - an index into the list,
    /// rather than a name that happens to be short.
    bool isIndex(const std::string& spec)
    {
        return !spec.empty()
            && std::all_of(spec.begin(), spec.end(),
                           [](unsigned char c) { return std::isdigit(c) != 0; });
    }
}

MidiOutput::~MidiOutput()
{
    close();
}

#if defined(_WIN32)

// Windows has winmm's midiOut, and nothing on this rig asks for it yet: the
// lamps are a Linux desk's problem today. Stubbed rather than absent so the
// protocol command exists everywhere and says so, which is a better answer
// than a command that is missing on one platform for no visible reason.

std::vector<MidiOutPortInfo> MidiOutput::enumeratePorts() { return {}; }

bool MidiOutput::resolvePort(const std::string&, MidiOutPortInfo&, std::string& outError)
{
    outError = "midi out: not supported on this platform yet";
    return false;
}

bool MidiOutput::open(const std::string&, std::string& outError)
{
    outError = "midi out: not supported on this platform yet";
    return false;
}

bool MidiOutput::send(const unsigned char*, size_t, std::string& outError)
{
    outError = "midi out: not supported on this platform yet";
    return false;
}

void MidiOutput::close() {}

#else

std::vector<MidiOutPortInfo> MidiOutput::enumeratePorts()
{
    std::vector<MidiOutPortInfo> ports;

    const AlsaRawMidi* alsa = AlsaRawMidi::get();
    if (alsa == nullptr)
    {
        // No libasound. Not an error - just a machine with nothing to light.
        return ports;
    }

    void* info = nullptr;
    if (alsa->infoMalloc(&info) < 0 || info == nullptr)
    {
        return ports;
    }

    int card = -1;
    while (alsa->cardNext(&card) >= 0 && card >= 0)
    {
        const std::string control = "hw:" + std::to_string(card);

        void* ctl = nullptr;
        if (alsa->ctlOpen(&ctl, control.c_str(), 0) < 0)
        {
            continue;
        }

        int device = -1;
        while (alsa->ctlRawMidiNextDevice(ctl, &device) >= 0 && device >= 0)
        {
            alsa->infoSetDevice(info, static_cast<unsigned int>(device));
            alsa->infoSetSubdevice(info, 0);
            alsa->infoSetStream(info, alsarawmidi::kStreamOutput);
            if (alsa->ctlRawMidiInfo(ctl, info) < 0)
            {
                // This device has no output stream. A MIDI *input* only box -
                // a foot controller, a keyboard with no lamps - lands here.
                continue;
            }

            const unsigned int count = alsa->infoGetSubdevicesCount(info);
            for (unsigned int sub = 0; sub < count; ++sub)
            {
                alsa->infoSetSubdevice(info, sub);
                if (alsa->ctlRawMidiInfo(ctl, info) < 0)
                {
                    continue;
                }

                // The subdevice name is the useful one, and the reason this
                // walk goes all the way down: a Launchpad is one card and one
                // device with two subdevices, and "LPX DAW Out" and "LPX MIDI
                // Out" are the only thing telling them apart. Devices with a
                // single subdevice usually leave it blank and the device name
                // is what anyone would recognise.
                const char* subName = alsa->infoGetSubdeviceName(info);
                const char* devName = alsa->infoGetName(info);
                std::string name = (subName != nullptr && subName[0] != '\0')
                    ? subName
                    : (devName != nullptr ? devName : "");
                if (name.empty())
                {
                    name = "card " + std::to_string(card);
                }

                MidiOutPortInfo port;
                port.index = static_cast<int>(ports.size());
                port.name = name;
                port.address = "hw:" + std::to_string(card) + ","
                             + std::to_string(device) + "," + std::to_string(sub);
                ports.push_back(port);
            }
        }

        alsa->ctlClose(ctl);
    }

    alsa->infoFree(info);
    return ports;
}

bool MidiOutput::resolvePort(const std::string& spec, MidiOutPortInfo& outPort,
                             std::string& outError)
{
    const std::vector<MidiOutPortInfo> ports = enumeratePorts();

    // A card address, taken verbatim. The unambiguous answer when two ports
    // share a name, and the one `amidi -l` prints - so it can be copied.
    if (spec.rfind("hw:", 0) == 0)
    {
        const auto found = std::find_if(ports.begin(), ports.end(),
            [&spec](const MidiOutPortInfo& port) { return port.address == spec; });
        outPort = (found != ports.end()) ? *found : MidiOutPortInfo{};
        if (found == ports.end())
        {
            // Not enumerated, but still opened: a subdevice can exist without
            // showing up in a walk on some drivers, and refusing to open an
            // address the user read off amidi would be its own puzzle.
            outPort.name = spec;
            outPort.address = spec;
        }
        return true;
    }

    if (ports.empty())
    {
        outError = "no midi outputs on this machine";
        return false;
    }

    if (isIndex(spec))
    {
        const int index = std::stoi(spec);
        if (index < 0 || index >= static_cast<int>(ports.size()))
        {
            outError = "no midi output " + spec + "; there are "
                     + std::to_string(ports.size());
            return false;
        }
        outPort = ports[static_cast<size_t>(index)];
        return true;
    }

    if (spec == "auto")
    {
        if (ports.size() != 1)
        {
            // The same refusal the input side makes, for the same reason: a
            // guess that picks wrong lights the wrong box, and the failure
            // looks identical to a dead cable.
            outError = "midi out: " + std::to_string(ports.size())
                     + " outputs; name one (midi list shows them)";
            return false;
        }
        outPort = ports.front();
        return true;
    }

    // An exact name first, so a port whose full name is a fragment of another's
    // is still reachable.
    for (const MidiOutPortInfo& port : ports)
    {
        if (port.name == spec)
        {
            outPort = port;
            return true;
        }
    }

    const MidiOutPortInfo* match = nullptr;
    for (const MidiOutPortInfo& port : ports)
    {
        if (contains(port.name, spec))
        {
            if (match != nullptr)
            {
                outError = "midi out: '" + spec + "' matches more than one output";
                return false;
            }
            match = &port;
        }
    }

    if (match == nullptr)
    {
        outError = "midi out: no output matching '" + spec + "'";
        return false;
    }

    outPort = *match;
    return true;
}

bool MidiOutput::open(const std::string& spec, std::string& outError)
{
    const AlsaRawMidi* alsa = AlsaRawMidi::get();
    if (alsa == nullptr)
    {
        outError = "midi out: no libasound on this machine";
        return false;
    }

    MidiOutPortInfo port;
    if (!resolvePort(spec, port, outError))
    {
        return false;
    }

    close();

    void* opened = nullptr;
    const int result = alsa->open(nullptr, &opened, port.address.c_str(),
                                  alsarawmidi::kModeBlocking);
    if (result < 0 || opened == nullptr)
    {
        const char* why = alsa->strerror(result);
        outError = "midi out: cannot open " + port.address + " ("
                 + (why != nullptr ? why : "unknown error") + ")";
        return false;
    }

    std::lock_guard<std::mutex> held(lock);
    handle = opened;
    portName = port.name;
    portAddress = port.address;
    sent = 0;
    return true;
}

void MidiOutput::close()
{
    void* closing = nullptr;
    {
        std::lock_guard<std::mutex> held(lock);
        closing = handle;
        handle = nullptr;
        portName.clear();
        portAddress.clear();
    }

    // Outside the lock: snd_rawmidi_close drains, which can block, and a
    // sender parked on the mutex behind it would be waiting on the device.
    if (closing != nullptr)
    {
        const AlsaRawMidi* alsa = AlsaRawMidi::get();
        if (alsa != nullptr)
        {
            alsa->close(closing);
        }
    }
}

bool MidiOutput::send(const unsigned char* data, size_t length, std::string& outError)
{
    if (data == nullptr || length == 0)
    {
        return true;
    }

    const AlsaRawMidi* alsa = AlsaRawMidi::get();
    if (alsa == nullptr)
    {
        outError = "midi out: no libasound on this machine";
        return false;
    }

    std::lock_guard<std::mutex> held(lock);
    if (handle == nullptr)
    {
        outError = "midi out: no port open";
        return false;
    }

    // Written in a loop rather than once: a blocking rawmidi write still
    // returns short when the device's buffer fills, and a SysEx long enough
    // to light a whole surface is exactly the message that fills it. Sending
    // the first half and calling it done is how a repaint lights three
    // quarters of a grid.
    size_t written = 0;
    while (written < length)
    {
        const long result = alsa->write(handle, data + written, length - written);
        if (result < 0)
        {
            const char* why = alsa->strerror(static_cast<int>(result));
            outError = std::string("midi out: write failed (")
                     + (why != nullptr ? why : "unknown error") + ")";
            return false;
        }
        if (result == 0)
        {
            outError = "midi out: write made no progress";
            return false;
        }
        written += static_cast<size_t>(result);
    }

    const int drained = alsa->drain(handle);
    if (drained < 0)
    {
        const char* why = alsa->strerror(drained);
        outError = std::string("midi out: drain failed (")
                 + (why != nullptr ? why : "unknown error") + ")";
        return false;
    }

    ++sent;
    return true;
}

#endif // _WIN32

bool MidiOutput::isOpen() const
{
    std::lock_guard<std::mutex> held(lock);
    return handle != nullptr;
}

std::string MidiOutput::getPortName() const
{
    std::lock_guard<std::mutex> held(lock);
    return portName;
}

std::string MidiOutput::describe() const
{
    std::lock_guard<std::mutex> held(lock);
    if (handle == nullptr)
    {
        return "port=none";
    }
    return "port=\"" + portName + "\" " + portAddress
         + " sent=" + std::to_string(sent);
}
