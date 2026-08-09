// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

///
/// eclipse-dmx
///
/// Takes an eclipse-os pattern, renders it into a DMX universe, and pushes it
/// at an Enttec USB widget. Meant to stand in for QLC+ on a fixed rig: one
/// config file, one process, no show-control stack in between.
///
/// stdout carries a line protocol so a supervisor (see python/) can drive a
/// running show. stderr carries logs, which keeps the two streams separable
/// when the process is being piped.
///

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "lib/ecore/hsv.h"
#include "lib/eio/hsv_strip.h"

#include "edmx/beat_clock.h"
#include "edmx/config.h"
#include "edmx/dmx_output.h"
#include "edmx/fixture.h"
#include "edmx/midi_input.h"
#include "edmx/pattern.h"
#include "edmx/serial_port.h"
#include "edmx/state_machine.h"

using namespace edmx;

namespace
{
    /// stdout is the machine channel: one line per event, always flushed so a
    /// parent process reading it never sits on a stale buffer.
    void emit(const std::string& line)
    {
        std::cout << line << "\n";
        std::cout.flush();
    }

    void logLine(const std::string& line)
    {
        std::cerr << line << "\n";
        std::cerr.flush();
    }

    void printUsage()
    {
        std::cerr <<
            "eclipse-dmx - render eclipse-os patterns to an Enttec USB DMX widget\n"
            "\n"
            "usage:\n"
            "  eclipse-dmx --config <file.json> [options]\n"
            "  eclipse-dmx --list-ports\n"
            "  eclipse-dmx --list-midi\n"
            "  eclipse-dmx --midi-selftest\n"
            "  eclipse-dmx --list-patterns\n"
            "  eclipse-dmx --list-palettes\n"
            "  eclipse-dmx --list-profiles\n"
            "\n"
            "options:\n"
            "  --config <file>     config file to load (required to run a show)\n"
            "  --dry-run           ignore device.type and print frames to stderr\n"
            "  --frames <n>        render n frames then exit (0 = run until stopped)\n"
            "  --port <path>       override device.port\n"
            "  --device <type>     override device.type (enttec_pro, enttec_open, console)\n"
            "  --pattern <name>    override pattern.name\n"
            "  --state <name>      open on this state, for a state machine pattern\n"
            "  --fps <n>           override device.fps\n"
            "  --midi <spec>       open a MIDI input for tempo (index, name or auto)\n"
            "  --no-midi           ignore the config's midi block\n"
            "  --bpm <n>           starting tempo, and the free-run fallback\n"
            "  --show-patch        print the resolved channel map and exit\n"
            "  --emit-frames       stream per-fixture rgb on stdout, for a viewer\n"
            "  --emit-rate <n>     cap that stream at n per second (default 30)\n"
            "  --no-stdin          do not read the control protocol from stdin\n"
            "  --verbose           log every state change\n"
            "  --help              this text\n"
            "\n"
            "control protocol (one command per line on stdin):\n"
            "  pattern <name>            switch pattern\n"
            "  state <name>              switch a state machine to one of its looks\n"
            "  states                    list the running pattern's looks\n"
            "  input <a|b> <on|off>      the two momentary inputs a relic look reads\n"
            "  speed <float>             pattern speed\n"
            "  width <float>             pattern width\n"
            "  brightness <float>        pattern brightness (0..1)\n"
            "  master <float>            master brightness (0..1)\n"
            "  color <h> <s> <v>         colour for solid/pulse, h in degrees\n"
            "  palette <name|#hex,...>   named palette or an explicit stop list\n"
            "  blackout <on|off>         hold the rig dark without losing the look\n"
            "  bpm <float>               set the tempo by hand\n"
            "  beat                      a downbeat, now - tap it, or trigger a cue\n"
            "  midi list                 MIDI inputs the machine can see\n"
            "  midi open <spec>          follow tempo from that input\n"
            "  midi close                stop following, keep the tempo\n"
            "  midi align                the downbeat is now (for clock with no start)\n"
            "  midi free-run <on|off>    keep pulsing when the clock stops\n"
            "  midi monitor <on|off>     print every message arriving, to identify a mapping\n"
            "  midi status               port, tempo, lock\n"
            "  status                    report current state\n"
            "  quit                      shut down, sending one dark frame first\n"
            "\n";
    }

    /// The message kind, spelled out. `MIDI-IN ch=1 note_on 50 100` is readable
    /// at a glance; `MIDI-IN ch=1 144 50 100` is a lookup table away from it.
    const char* describeMidiKind(int kind)
    {
        switch (kind)
        {
            case 0x80: return "note_off";
            case 0x90: return "note_on";
            case 0xA0: return "aftertouch";
            case 0xB0: return "cc";
            case 0xC0: return "program";
            case 0xD0: return "pressure";
            case 0xE0: return "pitchbend";
            default:   break;
        }
        return "other";
    }

    /// Drives the MIDI handling with a synthesised Mixxx stream and checks what
    /// comes out. Returns 0 on success.
    ///
    /// Every step of wiring up a light rig is verifiable except one: whether
    /// the notes arriving are the notes we think they are. That last step needs
    /// the actual software. What this covers is everything downstream of it —
    /// given those messages, do we get the right beats at the right tempo, and
    /// do we correctly ignore everything else on the cable.
    ///
    /// It runs in synthetic time. handleMessage() takes the timestamp rather
    /// than reading a clock, so a whole minute of a set replays instantly and
    /// identically every run.
    int runMidiSelfTest()
    {
        int failures = 0;

        const auto check = [&failures](const char* what, double got, double expected,
                                       double tolerance) {
            std::ostringstream line;
            line.setf(std::ios::fixed);
            line.precision(3);
            const bool ok = std::fabs(got - expected) <= tolerance;
            line << (ok ? "SELFTEST ok   " : "SELFTEST FAIL ") << what
                 << " got=" << got << " expected=" << expected;
            emit(line.str());
            if (!ok)
            {
                ++failures;
            }
        };

        // ---- a Mixxx MIDI-for-light stream, with everything it sends -------
        {
            BeatClock clock;
            MidiInput midi;
            midi.setBeatClock(&clock);

            // Start deliberately wrong, so "the tempo arrived" is distinguishable
            // from "the tempo was already right".
            clock.setBpm(100.0f, BeatSource::Internal, 0.0);

            constexpr double kBpm = 128.0;
            constexpr double kPeriod = 60.0 / kBpm;
            constexpr int kBeats = 16;

            for (int beat = 0; beat < kBeats; ++beat)
            {
                const double at = 10.0 + (beat * kPeriod);

                // note 52: the tempo, as velocity + 50
                midi.handleMessage(0x90, 52, static_cast<unsigned char>(kBpm - 50.0), at - 0.002);
                // note 50: the beat
                midi.handleMessage(0x90, 50, 100, at);
                midi.handleMessage(0x80, 50, 0, at + 0.05);
                // note 48: deck change, occasionally
                if (beat == 8)
                {
                    midi.handleMessage(0x90, 48, 101, at + 0.01);
                }
                // notes 64..76: VU meters, the whole point of the note filter
                for (int vu = 0x40; vu <= 0x4C; ++vu)
                {
                    midi.handleMessage(0x90, static_cast<unsigned char>(vu),
                                       static_cast<unsigned char>(30 + (vu & 0x0F)),
                                       at + 0.02 + (vu - 0x40) * 0.001);
                }
            }

            const double end = 10.0 + ((kBeats - 1) * kPeriod);

            check("mixxx.beats", static_cast<double>(midi.getBeats()), kBeats, 0.0);
            check("mixxx.bpm", clock.getBpm(), kBpm, 0.5);
            check("mixxx.phase_on_beat", clock.timeSinceBeat(end), 0.0, 0.001);
            check("mixxx.locked", clock.isLocked(end) ? 1.0 : 0.0, 1.0, 0.0);

            // Half a beat later the envelope should be half a beat in, which is
            // what a pattern actually reads.
            check("mixxx.phase_mid_beat", clock.timeSinceBeat(end + (kPeriod / 2.0)),
                  kPeriod / 2.0, 0.001);
        }

        // ---- beats with no tempo message: learn it from the gaps -----------
        {
            BeatClock clock;
            MidiInput midi;
            midi.setBeatClock(&clock);
            midi.setBpmNote(-1);
            clock.setBpm(100.0f, BeatSource::Internal, 0.0);

            constexpr double kBpm = 140.0;
            constexpr double kPeriod = 60.0 / kBpm;

            for (int beat = 0; beat < 40; ++beat)
            {
                midi.handleMessage(0x90, 50, 100, 10.0 + (beat * kPeriod));
            }

            // Smoothed, so it converges rather than snapping. 40 beats is a
            // few seconds of music and should be well inside a bpm.
            check("measured.bpm", clock.getBpm(), kBpm, 1.0);
        }

        // ---- beat clock, for sources that send it --------------------------
        {
            BeatClock clock;
            MidiInput midi;
            midi.setBeatClock(&clock);

            constexpr double kBpm = 120.0;
            constexpr double kTick = 60.0 / kBpm / 24.0;

            double at = 5.0;
            midi.handleMessage(0xFA, 0, 0, at); // start: the downbeat is here
            for (int tick = 0; tick < 24 * 8; ++tick)
            {
                midi.handleMessage(0xF8, 0, 0, at);
                at += kTick;
            }

            check("clock.ticks", static_cast<double>(midi.getClockTicks()), 24 * 8, 0.0);
            check("clock.beats", static_cast<double>(midi.getBeats()), 8, 0.0);
            check("clock.bpm", clock.getBpm(), kBpm, 1.0);
        }

        // ---- notes win over clock, when a source sends both ----------------
        {
            BeatClock clock;
            MidiInput midi;
            midi.setBeatClock(&clock);

            constexpr double kPeriod = 0.5;
            constexpr double kTick = kPeriod / 24.0;

            double at = 5.0;
            for (int tick = 0; tick < 24 * 4; ++tick)
            {
                if (tick % 24 == 0)
                {
                    midi.handleMessage(0x90, 50, 100, at); // a note beat, first
                }
                midi.handleMessage(0xF8, 0, 0, at);
                at += kTick;
            }

            // Four beats, not eight: the clock ticks must not double them up.
            check("both.beats", static_cast<double>(midi.getBeats()), 4, 0.0);
        }

        // ---- the raw byte parser, which is the linux path ------------------
        {
            BeatClock clock;
            MidiInput midi;
            midi.setBeatClock(&clock);

            // Running status - one 0x90, then bare note/velocity pairs - with a
            // realtime byte shoved into the middle of a message, which the spec
            // allows and which is exactly what breaks a naive parser.
            const unsigned char stream[] = {
                0x90, 50, 100,        // note on, beat
                0x40, 90,             // running status: a VU note
                50, 0xF8, 100,        // another beat, with a clock tick mid-message
                0x4C, 12,             // more VU
            };
            midi.handleBytes(stream, sizeof(stream), 20.0);

            check("bytes.beats", static_cast<double>(midi.getBeats()), 2, 0.0);
            check("bytes.ticks", static_cast<double>(midi.getClockTicks()), 1, 0.0);
        }

        // ---- the ignore list -----------------------------------------------
        {
            const std::vector<std::string> ignore = {"Traktor", "Kontrol"};
            check("ignore.matches", MidiInput::isIgnored("Traktor Kontrol S2 MK3", ignore) ? 1.0 : 0.0,
                  1.0, 0.0);
            check("ignore.spares_others", MidiInput::isIgnored("loopMIDI Port", ignore) ? 1.0 : 0.0,
                  0.0, 0.0);
        }

        if (failures == 0)
        {
            emit("OK midi selftest passed");
            return 0;
        }

        emit("ERR midi selftest: " + std::to_string(failures) + " checks failed");
        return 1;
    }

    std::vector<std::string> splitWords(const std::string& line)
    {
        std::vector<std::string> words;
        std::istringstream stream(line);
        std::string word;
        while (stream >> word)
        {
            words.push_back(word);
        }
        return words;
    }

    bool parseFloatArg(const std::string& text, float& outValue)
    {
        try
        {
            size_t consumed = 0;
            const float parsed = std::stof(text, &consumed);
            if (consumed != text.size())
            {
                return false;
            }
            outValue = parsed;
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    /// Reads stdin on its own thread and hands whole lines to the show loop.
    /// Blocking getline on the render thread would stall the DMX refresh, and a
    /// rig that stops refreshing is a rig that some fixtures will time out on.
    class StdinReader
    {
    public:
        void start()
        {
            running = true;
            thread = std::thread([this]() {
                std::string line;
                while (std::getline(std::cin, line))
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    pending.push_back(line);
                }
                // stdin closed: the parent went away or the user hit ctrl-d
                eof = true;
            });
        }

        void stop()
        {
            running = false;
            if (thread.joinable())
            {
                // std::cin has no portable interrupt, so we leave the reader
                // detached rather than hang the shutdown waiting on a line
                // that is never going to arrive.
                thread.detach();
            }
        }

        std::vector<std::string> drain()
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::vector<std::string> out;
            out.swap(pending);
            return out;
        }

        bool isEof() const { return eof; }

    private:
        std::thread thread;
        std::mutex mutex;
        std::vector<std::string> pending;
        std::atomic<bool> running{false};
        std::atomic<bool> eof{false};
    };

    /// Holds everything a running show mutates, so the command handler and the
    /// render loop have one place to agree on.
    struct ShowState
    {
        Config config;
        std::unique_ptr<Pattern> pattern;
        std::unique_ptr<DmxOutput> output;
        std::unique_ptr<eio::HSVStrip> strip;
        DmxUniverse universe;
        PatternContext context;
        std::vector<ecore::HSV> colors;

        float masterBrightness{1.0f};
        bool blackout{false};
        bool verbose{false};
        bool shouldQuit{false};

        /// The two momentary inputs a relic look can read, held here so they
        /// survive a state change.
        bool inputA{false};
        bool inputB{false};

        /// Tempo in, when there is any. The beat itself lives in
        /// sharedBeatClock(), which is what patterns read; this is only the
        /// device feeding it.
        MidiInput midi;
    };

    /// Resolves the coordinate frame for whatever pattern is running.
    ///
    /// Re-run on every pattern switch, not just at startup: an obelisk look and
    /// a jacket look want quite different spaces, and carrying one into the
    /// other renders a flat wash.
    void applyCoordFrame(ShowState& show)
    {
        if (!show.pattern)
        {
            return;
        }

        show.context.coords = show.pattern->defaultCoordFrame();

        const PatternConfig& cfg = show.config.pattern;
        if (cfg.coordOriginX) show.context.coords.originX = *cfg.coordOriginX;
        if (cfg.coordOriginY) show.context.coords.originY = *cfg.coordOriginY;
        if (cfg.coordSpanX)   show.context.coords.spanX   = *cfg.coordSpanX;
        if (cfg.coordSpanY)   show.context.coords.spanY   = *cfg.coordSpanY;
    }

    /// Tells a UI which states it can offer, and which is showing.
    ///
    /// Emitted whenever the pattern changes, so a client that switches to a
    /// state machine learns its states without having to ask.
    void emitStates(ShowState& show)
    {
        StateMachinePattern* machine = show.pattern ? show.pattern->asStateMachine() : nullptr;
        if (!machine)
        {
            emit("STATES");
            return;
        }

        std::ostringstream out;
        out << "STATES";
        for (const std::string& state : machine->stateNames())
        {
            out << " " << state;
        }
        emit(out.str());
        emit("STATE " + machine->currentStateName());
    }

    /// Lists what the machine can hear, for `midi list` and `--list-midi`.
    ///
    /// Ports on the ignore list are shown, not hidden. Knowing that the one
    /// device you expected is being skipped on purpose is the whole point.
    void emitMidiPorts(const std::vector<std::string>& ignore = {})
    {
        const std::vector<MidiPortInfo> ports = MidiInput::enumeratePorts();
        for (const MidiPortInfo& port : ports)
        {
            emit("MIDI " + std::to_string(port.index) + "\t" + port.name
               + (MidiInput::isIgnored(port.name, ignore) ? "\tignored" : ""));
        }
        emit("OK " + std::to_string(ports.size()) + " midi inputs");
    }

    std::string describeState(const ShowState& show)
    {
        StateMachinePattern* machine = show.pattern ? show.pattern->asStateMachine() : nullptr;

        std::ostringstream out;
        out << "STATUS pattern=" << (show.pattern ? show.pattern->getName() : "none")
            << " state=" << (machine ? machine->currentStateName() : "-")
            << " speed=" << (show.pattern ? show.pattern->getSpeed() : 0.0f)
            << " width=" << (show.pattern ? show.pattern->getWidth() : 0.0f)
            << " brightness=" << (show.pattern ? show.pattern->getBrightness() : 0.0f)
            << " master=" << show.masterBrightness
            << " blackout=" << (show.blackout ? "on" : "off")
            << " fixtures=" << show.config.fixtures.size()
            << " fps=" << show.config.device.fps
            << " " << sharedBeatClock().describe(nowSeconds())
            << " midi=" << (show.midi.isOpen() ? ("\"" + show.midi.getPortName() + "\"") : "none")
            << " output=" << (show.output ? show.output->describe() : "none");
        return out.str();
    }

    /// Applies one line of the control protocol. Replies on stdout with OK or
    /// ERR so the wrapper can tell whether a command took.
    void handleCommand(ShowState& show, const std::string& line)
    {
        const std::vector<std::string> words = splitWords(line);
        if (words.empty())
        {
            return;
        }

        const std::string& command = words[0];

        if (command == "quit" || command == "exit")
        {
            show.shouldQuit = true;
            emit("OK quit");
            return;
        }

        if (command == "status")
        {
            emit(describeState(show));
            return;
        }

        if (command == "pattern")
        {
            if (words.size() < 2)
            {
                emit("ERR pattern needs a name");
                return;
            }

            // Carry the live values across the switch: changing look should not
            // silently reset the speed someone just dialled in.
            PatternConfig next = show.config.pattern;
            next.name = words[1];
            if (show.pattern)
            {
                next.speed = show.pattern->getSpeed();
                next.width = show.pattern->getWidth();
                next.brightness = show.pattern->getBrightness();
            }

            std::string error;
            std::unique_ptr<Pattern> created = makePattern(next.name, next, error);
            if (!created)
            {
                emit("ERR " + error);
                return;
            }

            // keep whatever palette/colour the running show had
            if (show.pattern)
            {
                created->setPalette(resolvePalette(show.config.pattern));
                created->setColor(show.config.pattern.solidColor);
            }

            show.pattern = std::move(created);
            show.config.pattern.name = next.name;

            // A new pattern brings its own coordinate space and, if it is a
            // state machine, its own set of states.
            applyCoordFrame(show);
            emit("OK pattern " + next.name);
            emitStates(show);
            return;
        }

        if (command == "state")
        {
            StateMachinePattern* machine = show.pattern ? show.pattern->asStateMachine() : nullptr;
            if (!machine)
            {
                emit("ERR pattern '" + std::string(show.pattern ? show.pattern->getName() : "none")
                   + "' is not a state machine");
                return;
            }
            if (words.size() < 2)
            {
                emit("ERR state needs a name");
                return;
            }

            std::string error;
            if (!machine->setState(words[1], error))
            {
                emit("ERR " + error);
                return;
            }

            emit("OK state " + words[1]);
            emit("STATE " + machine->currentStateName());
            return;
        }

        if (command == "states")
        {
            emitStates(show);
            return;
        }

        if (command == "input")
        {
            // input <a|b> <on|off> - the momentary inputs relic looks read.
            // On a jacket these are remote buttons; here a UI drives them.
            StateMachinePattern* machine = show.pattern ? show.pattern->asStateMachine() : nullptr;
            if (!machine)
            {
                emit("ERR input needs a state machine pattern");
                return;
            }
            if (words.size() < 3)
            {
                emit("ERR input needs a channel (a|b) and on|off");
                return;
            }

            const bool down = (words[2] == "on" || words[2] == "1" || words[2] == "true");
            if (words[1] == "a")      show.inputA = down;
            else if (words[1] == "b") show.inputB = down;
            else
            {
                emit("ERR input channel must be a or b, got '" + words[1] + "'");
                return;
            }

            machine->setInput(show.inputA, show.inputB);
            emit("OK input " + words[1] + " " + (down ? "on" : "off"));
            return;
        }

        // ---- tempo -------------------------------------------------------
        // The beat clock is process-wide rather than owned by a pattern, so
        // these work whatever is running: dial the tempo in on `solid`, then
        // switch to a look that uses it and it is already right.
        if (command == "bpm")
        {
            if (words.size() < 2)
            {
                emit("ERR bpm needs a value");
                return;
            }

            float value = 0.0f;
            if (!parseFloatArg(words[1], value))
            {
                emit("ERR bpm: '" + words[1] + "' is not a number");
                return;
            }
            if (value < BeatClock::kMinBpm || value > BeatClock::kMaxBpm)
            {
                emit("ERR bpm must be between " + std::to_string(static_cast<int>(BeatClock::kMinBpm))
                   + " and " + std::to_string(static_cast<int>(BeatClock::kMaxBpm)));
                return;
            }

            sharedBeatClock().setBpm(value, BeatSource::Internal);
            emit("OK bpm " + words[1]);
            return;
        }

        if (command == "beat")
        {
            // A downbeat, now. Two jobs in one: tapping the tempo in when there
            // is no MIDI, and telling a running clock where the bar starts when
            // it only sends 0xF8 and never said.
            const double when = nowSeconds();
            sharedBeatClock().markBeat(when, BeatSource::Manual);
            show.midi.alignToNow();
            emit("OK beat");
            return;
        }

        if (command == "midi")
        {
            const std::string action = (words.size() >= 2) ? words[1] : "status";

            if (action == "list")
            {
                emitMidiPorts(show.config.midi.ignore);
                return;
            }

            if (action == "status")
            {
                emit("MIDI-STATUS " + show.midi.describe() + " "
                   + sharedBeatClock().describe(nowSeconds()));
                emit("OK midi status");
                return;
            }

            if (action == "monitor")
            {
                const bool enable = (words.size() < 3)
                    || (words[2] == "on" || words[2] == "1" || words[2] == "true");
                show.midi.setMonitor(enable);
                emit(std::string("OK midi monitor ") + (enable ? "on" : "off"));
                return;
            }

            if (action == "open")
            {
                const std::string spec = (words.size() >= 3) ? words[2] : "auto";

                std::string midiError;
                if (!show.midi.open(spec, show.config.midi.ignore, &sharedBeatClock(), midiError))
                {
                    emit("ERR " + midiError);
                    return;
                }

                show.midi.setFollowClock(show.config.midi.followClock);
                show.midi.setFollowNotes(show.config.midi.followNotes);
                show.midi.setBeatNote(show.config.midi.beatNote);
                show.midi.setBpmNote(show.config.midi.bpmNote);
                show.midi.setBeatChannel(show.config.midi.beatChannel);

                emit("OK midi open " + show.midi.getPortName());
                return;
            }

            if (action == "close")
            {
                show.midi.close();
                // The tempo stays where it was on purpose: closing the link
                // should not also stop the show.
                emit("OK midi close");
                return;
            }

            if (action == "align")
            {
                sharedBeatClock().restart(nowSeconds(), BeatSource::Manual);
                show.midi.alignToNow();
                emit("OK midi align");
                return;
            }

            if (action == "free-run" || action == "free_run")
            {
                const bool enable = (words.size() < 3)
                    || (words[2] == "on" || words[2] == "1" || words[2] == "true");
                sharedBeatClock().setFreeRun(enable);
                emit(std::string("OK midi free-run ") + (enable ? "on" : "off"));
                return;
            }

            emit("ERR midi: expected list, open, close, align, free-run, monitor or status, "
                 "got '" + action + "'");
            return;
        }

        if (command == "speed" || command == "width" || command == "brightness" || command == "master")
        {
            if (words.size() < 2)
            {
                emit("ERR " + command + " needs a value");
                return;
            }

            float value = 0.0f;
            if (!parseFloatArg(words[1], value))
            {
                emit("ERR " + command + ": '" + words[1] + "' is not a number");
                return;
            }

            if (command == "speed")           show.pattern->setSpeed(value);
            else if (command == "width")      show.pattern->setWidth(std::max(value, 0.001f));
            else if (command == "brightness") show.pattern->setBrightness(std::clamp(value, 0.0f, 1.0f));
            else                              show.masterBrightness = std::clamp(value, 0.0f, 1.0f);

            emit("OK " + command + " " + words[1]);
            return;
        }

        if (command == "color" || command == "colour")
        {
            // either three numbers (h s v) or a single hex string
            if (words.size() == 2)
            {
                ecore::HSV parsed;
                if (!parseColorString(words[1], parsed))
                {
                    emit("ERR color: '" + words[1] + "' is not a hex colour");
                    return;
                }
                show.config.pattern.solidColor = parsed;
                show.pattern->setColor(parsed);
                emit("OK color " + words[1]);
                return;
            }

            if (words.size() >= 4)
            {
                float h = 0.0f;
                float s = 0.0f;
                float v = 0.0f;
                if (!parseFloatArg(words[1], h) || !parseFloatArg(words[2], s) || !parseFloatArg(words[3], v))
                {
                    emit("ERR color: expected three numbers (h s v)");
                    return;
                }
                const ecore::HSV parsed(h, s, v);
                show.config.pattern.solidColor = parsed;
                show.pattern->setColor(parsed);
                emit("OK color");
                return;
            }

            emit("ERR color needs '#rrggbb' or 'h s v'");
            return;
        }

        if (command == "palette")
        {
            if (words.size() < 2)
            {
                emit("ERR palette needs a name or a comma separated stop list");
                return;
            }

            const std::string& argument = words[1];

            if (argument.find(',') == std::string::npos && argument.find('#') == std::string::npos)
            {
                ecore::HSVPalette named;
                if (!lookupNamedPalette(argument, named))
                {
                    emit("ERR unknown palette '" + argument + "'");
                    return;
                }
                show.config.pattern.palette.clear();
                show.config.pattern.paletteName = argument;
                show.pattern->setPalette(named);
                emit("OK palette " + argument);
                return;
            }

            std::vector<ecore::HSV> stops;
            std::istringstream stream(argument);
            std::string token;
            while (std::getline(stream, token, ','))
            {
                ecore::HSV parsed;
                if (!parseColorString(token, parsed))
                {
                    emit("ERR palette: '" + token + "' is not a hex colour");
                    return;
                }
                stops.push_back(parsed);
            }

            if (stops.size() < 2)
            {
                emit("ERR palette needs at least two stops");
                return;
            }

            show.config.pattern.palette = stops;
            show.pattern->setPalette(ecore::HSVPalette(stops));
            emit("OK palette " + std::to_string(stops.size()) + " stops");
            return;
        }

        if (command == "blackout")
        {
            const bool enable = (words.size() < 2) || (words[1] == "on" || words[1] == "1" || words[1] == "true");
            show.blackout = enable;
            emit(std::string("OK blackout ") + (enable ? "on" : "off"));
            return;
        }

        emit("ERR unknown command '" + command + "'");
    }
}

int main(int argc, char** argv)
{
    std::string configPath;
    std::string portOverride;
    std::string deviceOverride;
    std::string patternOverride;
    std::string stateOverride;
    std::string midiOverride;
    bool noMidi = false;
    float bpmOverride = 0.0f;
    float fpsOverride = 0.0f;
    long long frameLimit = 0;
    bool dryRun = false;
    bool useStdin = true;
    bool verbose = false;
    bool showPatch = false;
    bool emitFrames = false;
    float emitRate = 30.0f;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto nextArg = [&](const char* name) -> std::string {
            if (i + 1 >= argc)
            {
                logLine(std::string("error: ") + name + " needs a value");
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return 0;
        }
        else if (arg == "--config" || arg == "-c") configPath = nextArg("--config");
        else if (arg == "--port")                  portOverride = nextArg("--port");
        else if (arg == "--device")                deviceOverride = nextArg("--device");
        else if (arg == "--pattern")               patternOverride = nextArg("--pattern");
        else if (arg == "--state")                 stateOverride = nextArg("--state");
        else if (arg == "--midi")                  midiOverride = nextArg("--midi");
        else if (arg == "--no-midi")               noMidi = true;
        else if (arg == "--bpm")                   bpmOverride = std::stof(nextArg("--bpm"));
        else if (arg == "--fps")                   fpsOverride = std::stof(nextArg("--fps"));
        else if (arg == "--frames")                frameLimit = std::stoll(nextArg("--frames"));
        else if (arg == "--dry-run")               dryRun = true;
        else if (arg == "--show-patch")            showPatch = true;
        else if (arg == "--emit-frames")           emitFrames = true;
        else if (arg == "--emit-rate")             emitRate = std::stof(nextArg("--emit-rate"));
        else if (arg == "--no-stdin")              useStdin = false;
        else if (arg == "--verbose")               verbose = true;
        else if (arg == "--list-ports")
        {
            const std::vector<SerialPortInfo> ports = SerialPort::enumeratePorts();
            for (const SerialPortInfo& info : ports)
            {
                emit("PORT " + info.path + "\t" + info.description);
            }
            emit("OK " + std::to_string(ports.size()) + " ports");
            return 0;
        }
        else if (arg == "--list-midi")
        {
            emitMidiPorts();
            return 0;
        }
        else if (arg == "--midi-selftest")
        {
            return runMidiSelfTest();
        }
        else if (arg == "--list-patterns")
        {
            for (const std::string& name : patternNames())
            {
                emit("PATTERN " + name);
            }
            return 0;
        }
        else if (arg == "--list-palettes")
        {
            for (const std::string& name : builtinPaletteNames())
            {
                emit("PALETTE " + name);
            }
            return 0;
        }
        else if (arg == "--list-profiles")
        {
            for (const std::string& name : builtinProfileNames())
            {
                FixtureProfile profile;
                if (lookupBuiltinProfile(name, profile))
                {
                    emit("PROFILE " + describeProfile(profile));
                }
            }
            return 0;
        }
        else
        {
            logLine("error: unknown argument '" + arg + "'");
            printUsage();
            return 2;
        }
    }

    if (configPath.empty())
    {
        logLine("error: --config is required");
        printUsage();
        return 2;
    }

    ShowState show;
    show.verbose = verbose;

    std::string error;
    if (!loadConfig(configPath, show.config, error))
    {
        logLine("config error: " + error);
        emit("ERR config " + error);
        return 1;
    }

    for (const std::string& warning : show.config.warnings)
    {
        logLine("warning: " + warning);
        emit("WARN " + warning);
    }

    // ---- patch report ----------------------------------------------------
    // What actually got resolved, per fixture. On a rig of identical pars
    // patched from one profile line, this is the only way to see the addresses
    // without counting them out by hand.
    if (showPatch)
    {
        // Report in the config's own numbering, so these line up with what is
        // dialled on the fixtures rather than with our internal 1-based slots.
        const int bias = (show.config.addressing == Addressing::ZeroBased) ? -1 : 0;

        emit(std::string("ADDRESSING ")
           + ((show.config.addressing == Addressing::ZeroBased) ? "zero" : "one") + "-based");

        int highest = 0;
        for (const Fixture& fixture : show.config.fixtures.all())
        {
            std::ostringstream line;
            line << "PATCH " << fixture.name
                 << " r=" << (fixture.startChannel + fixture.offsetR + bias)
                 << " g=" << (fixture.startChannel + fixture.offsetG + bias)
                 << " b=" << (fixture.startChannel + fixture.offsetB + bias);

            if (fixture.dimmerChannel > 0)
            {
                line << " dimmer=" << (fixture.dimmerChannel + bias)
                     << "@" << static_cast<int>(fixture.dimmerValue);
            }
            for (const auto& entry : fixture.staticChannels)
            {
                line << " park[" << (entry.first + bias) << "]=" << static_cast<int>(entry.second);
            }

            emit(line.str());
            highest = std::max(highest, fixtureHighestChannel(fixture));
        }

        emit("OK " + std::to_string(show.config.fixtures.size())
           + " fixtures, highest channel " + std::to_string(highest + bias));
        return 0;
    }

    if (!portOverride.empty())    show.config.device.port = portOverride;
    if (!deviceOverride.empty())  show.config.device.type = deviceOverride;
    if (!patternOverride.empty()) show.config.pattern.name = patternOverride;
    if (!stateOverride.empty())   show.config.pattern.stateName = stateOverride;
    if (fpsOverride > 0.0f)       show.config.device.fps = fpsOverride;

    // Naming a port on the command line is asking for it, whatever the config
    // says; --no-midi is the way back out.
    if (!midiOverride.empty())
    {
        show.config.midi.enabled = true;
        show.config.midi.port = midiOverride;
    }
    if (bpmOverride > 0.0f)       show.config.midi.bpm = bpmOverride;
    if (noMidi)                   show.config.midi.enabled = false;

    // last, so --dry-run wins over --device: asking for both means you want to
    // pick a widget type but not actually drive it yet.
    if (dryRun)                   show.config.device.type = "console";

    show.masterBrightness = show.config.master.brightness;

    // ---- resolve the port ------------------------------------------------
    std::string port = show.config.device.port;
    if (port == "auto")
    {
        port = autoDetectPort();
        if (port.empty() && show.config.device.type != "console")
        {
            logLine("error: device.port is \"auto\" but no serial port was found");
            emit("ERR no serial port found");
            return 1;
        }
        if (!port.empty())
        {
            logLine("auto-detected port " + port);
        }
    }

    // ---- build the output ------------------------------------------------
    show.output = makeDmxOutput(show.config.device.type, port, show.config.device.baud,
                                show.config.device.consoleChannels, error);
    if (!show.output)
    {
        logLine("output error: " + error);
        emit("ERR output " + error);
        return 1;
    }

    // ---- send only the channels the rig actually uses --------------------
    // A receiver keeps whatever it already had for slots that do not arrive,
    // so there is nothing to gain from shipping 442 trailing zeros every
    // frame. On a serial link that padding is most of the frame time.
    {
        int highest = 0;
        for (const Fixture& fixture : show.config.fixtures.all())
        {
            highest = std::max(highest, fixtureHighestChannel(fixture));
        }
        show.output->setUniverseLength(highest);
    }

    if (!show.output->open(error))
    {
        logLine("output error: " + error);
        emit("ERR output " + error);
        return 1;
    }
    logLine("output: " + show.output->describe());

    // ---- do not outrun the wire ------------------------------------------
    // Asking for more frames than the link can carry does not make the rig
    // faster. The driver queues the excess, the backlog grows without bound,
    // and the widget ends up parsing half-written frames - which reads as a
    // strobing, unblendable rig rather than as an error. Render at a rate the
    // wire can actually deliver and every frame lands whole.
    const float outputCeiling = show.output->maxFrameRate();
    if (outputCeiling > 0.0f && show.config.device.fps > outputCeiling)
    {
        std::ostringstream note;
        note.setf(std::ios::fixed);
        note.precision(1);
        note << "device.fps " << show.config.device.fps
             << " is more than " << show.output->describe()
             << " can carry; running at " << outputCeiling
             << ". Raise device.baud for a faster refresh.";
        logLine("warning: " + note.str());
        emit("WARN " + note.str());

        show.config.device.fps = outputCeiling;
    }

    // ---- the beat --------------------------------------------------------
    // Set up before the first frame whether or not there is a MIDI device: the
    // clock free-runs, so a beat-driven look pulses at the configured tempo on
    // a bench with nothing plugged in, and locks to the music when there is
    // some. See edmx/beat_clock.h.
    sharedBeatClock().setBpm(show.config.midi.bpm, BeatSource::Internal);
    sharedBeatClock().setFreeRun(show.config.midi.freeRun);

    if (show.config.midi.enabled)
    {
        // Applied before the open, and whether or not it succeeds, so that
        // `midi status` reports what is *configured* rather than the class
        // defaults. Reading channel=-1 off a failed open, when the config
        // plainly says 1, sends you looking in the wrong place.
        show.midi.setFollowClock(show.config.midi.followClock);
        show.midi.setFollowNotes(show.config.midi.followNotes);
        show.midi.setBeatNote(show.config.midi.beatNote);
        show.midi.setBpmNote(show.config.midi.bpmNote);
        show.midi.setBeatChannel(show.config.midi.beatChannel);

        std::string midiError;
        if (show.midi.open(show.config.midi.port, show.config.midi.ignore,
                           &sharedBeatClock(), midiError))
        {
            logLine("midi: " + show.midi.describe());
            emit("MIDI-OPEN " + show.midi.getPortName());
        }
        else
        {
            // Not fatal. A rig that refuses to light because the DJ laptop is
            // not plugged in yet is a rig that fails at exactly the wrong
            // moment; free-run covers it and `midi open` fixes it live.
            logLine("warning: midi: " + midiError);
            emit("WARN midi: " + midiError);
        }
    }

    // ---- build the pattern -----------------------------------------------
    show.pattern = makePattern(show.config.pattern.name, show.config.pattern, error);
    if (!show.pattern)
    {
        logLine("pattern error: " + error);
        emit("ERR pattern " + error);
        return 1;
    }

    // ---- build the strip -------------------------------------------------
    // One HSV node per fixture. This is the same framebuffer the microcontroller
    // build hands to the LEDs; here it feeds the fixture patch instead.
    const size_t fixtureCount = show.config.fixtures.size();
    show.strip.reset(new eio::HSVStrip(static_cast<uint16_t>(fixtureCount), 0));

    show.context.fixtureCount = fixtureCount;
    applyCoordFrame(show);
    show.context.positions.resize(fixtureCount);
    for (size_t idx = 0; idx < fixtureCount; ++idx)
    {
        show.context.positions[idx] = show.config.fixtures.normalizedPosition(idx);
    }

    emit("READY fixtures=" + std::to_string(fixtureCount)
       + " pattern=" + show.config.pattern.name
       + " output=" + show.output->describe());

    // Open on the look the config asked for. Warn rather than fail: a typo here
    // should still light the rig, on the machine's own default.
    if (!show.config.pattern.stateName.empty())
    {
        StateMachinePattern* machine = show.pattern->asStateMachine();
        if (!machine)
        {
            logLine("warning: pattern.state is set but '" + show.config.pattern.name
                  + "' has no states; ignoring");
        }
        else
        {
            std::string stateError;
            if (!machine->setState(show.config.pattern.stateName, stateError))
            {
                logLine("warning: " + stateError);
                emit("WARN " + stateError);
            }
        }
    }

    // A state machine announces its states up front, so a UI can build its
    // buttons before the first frame lands.
    emitStates(show);

    if (emitFrames)
    {
        // Name the fixtures once, so the frame lines can stay compact.
        std::ostringstream names;
        names << "FIXTURES";
        for (const Fixture& fixture : show.config.fixtures.all())
        {
            names << " " << fixture.name;
        }
        emit(names.str());
    }

    StdinReader stdinReader;
    if (useStdin)
    {
        stdinReader.start();
    }

    // ---- the show loop ---------------------------------------------------
    using clock = std::chrono::steady_clock;
    const auto framePeriod = std::chrono::duration<double>(1.0 / static_cast<double>(show.config.device.fps));

    auto lastFrame = clock::now();
    auto nextFrame = lastFrame;
    auto nextEmit = lastFrame; // emit the first frame straight away
    long long framesRendered = 0;
    long long lastEmittedBeat = -1;
    int exitCode = 0;

    while (!show.shouldQuit)
    {
        if (useStdin)
        {
            for (const std::string& line : stdinReader.drain())
            {
                handleCommand(show, line);
            }
            if (show.shouldQuit)
            {
                break;
            }
            if (stdinReader.isEof() && frameLimit == 0)
            {
                // The parent closed the pipe. Keep running: an unattended rig
                // should not go dark because the supervisor detached.
            }
        }

        const auto now = clock::now();
        const float deltaTime = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;

        show.pattern->tick(deltaTime);
        show.pattern->render(show.context, show.colors);

        // Push through the strip so the desktop path and the microcontroller
        // path agree on what a frame is.
        for (size_t idx = 0; idx < fixtureCount && idx < show.colors.size(); ++idx)
        {
            show.strip->setHSV(static_cast<uint16_t>(idx), show.colors[idx]);
        }

        show.universe.clear();
        const float master = show.blackout ? 0.0f : show.masterBrightness;
        show.config.fixtures.render(show.strip->getStripHSV(), master, show.config.master.gamma, show.universe);

        if (!show.output->sendFrame(show.universe, error))
        {
            logLine("output error: " + error);
            emit("ERR output " + error);
            exitCode = 1;
            break;
        }

        // Whatever the MIDI monitor caught, drained here rather than emitted
        // from the device callback: that callback runs on a driver thread with
        // real restrictions, and writing to stdout from it would interleave
        // with the frame stream.
        if (show.midi.isMonitoring())
        {
            unsigned long long dropped = 0;
            for (const MidiMessage& message : show.midi.drainMonitor(dropped))
            {
                std::ostringstream line;
                line << "MIDI-IN ch=" << message.channel()
                     << " " << describeMidiKind(message.kind())
                     << " " << static_cast<int>(message.data1)
                     << " " << static_cast<int>(message.data2);
                emit(line.str());
            }
            if (dropped > 0)
            {
                emit("MIDI-IN dropped " + std::to_string(dropped));
            }
        }

        // A line per beat, for a UI that wants to show the tempo it is locked
        // to. Deliberately outside the frame rate limit below: a beat marker
        // that arrives whenever the next throttled frame happens to be due is
        // not a beat marker.
        if (emitFrames)
        {
            BeatClock& beat = sharedBeatClock();
            const double beatTime = nowSeconds();
            const long long beatNumber = static_cast<long long>(std::floor(beat.beatPosition(beatTime)));

            if (beatNumber != lastEmittedBeat)
            {
                lastEmittedBeat = beatNumber;

                std::ostringstream line;
                line.setf(std::ios::fixed);
                line.precision(1);
                line << "BEAT " << beatNumber << " " << beat.getBpm()
                     << " " << describeBeatSource(beat.getSource())
                     << " " << (beat.isLocked(beatTime) ? "lock" : "free");
                emit(line.str());
            }
        }

        // Stream what actually went out, for a viewer. Read back out of the
        // universe rather than off the pattern: a viewer fed from the pattern
        // would happily show a beautiful rig while the patch was wrong.
        if (emitFrames)
        {
            // Advance the target by exactly one period rather than restarting
            // it from now. Resetting to now rounds every wait up to the next
            // render tick, so asking for 30 on a 40fps show silently gives 20.
            // Accumulating lets the emitted rate average out to what was asked.
            const auto period = std::chrono::duration<float>(1.0f / std::max(emitRate, 0.1f));

            if (now >= nextEmit)
            {
                nextEmit += std::chrono::duration_cast<clock::duration>(period);

                // Never bank up a burst: after a stall, start counting again
                // from here instead of firing off every frame we owe.
                if (nextEmit < now)
                {
                    nextEmit = now + std::chrono::duration_cast<clock::duration>(period);
                }

                std::ostringstream frame;
                frame << "F";
                for (const Fixture& fixture : show.config.fixtures.all())
                {
                    char swatch[8];
                    std::snprintf(swatch, sizeof(swatch), " %02x%02x%02x",
                                  show.universe.getChannel(fixture.startChannel + fixture.offsetR),
                                  show.universe.getChannel(fixture.startChannel + fixture.offsetG),
                                  show.universe.getChannel(fixture.startChannel + fixture.offsetB));
                    frame << swatch;
                }
                emit(frame.str());
            }
        }

        ++framesRendered;
        if (frameLimit > 0 && framesRendered >= frameLimit)
        {
            break;
        }

        nextFrame += std::chrono::duration_cast<clock::duration>(framePeriod);
        const auto sleepFor = nextFrame - clock::now();
        if (sleepFor > clock::duration::zero())
        {
            std::this_thread::sleep_for(sleepFor);
        }
        else
        {
            // We fell behind. Resync rather than trying to catch up, which
            // would just burst frames at the widget.
            nextFrame = clock::now();
        }
    }

    // One dark frame on the way out, so the rig does not hold its last look
    // forever after we let go of it.
    show.universe.clear();
    show.config.fixtures.render(std::vector<ecore::HSV>(fixtureCount), 0.0f,
                                show.config.master.gamma, show.universe);
    std::string shutdownError;
    show.output->sendFrame(show.universe, shutdownError);
    show.output->close();

    // Before the reader goes, so the MIDI callback cannot fire into a clock
    // whose owner is on its way out.
    show.midi.close();

    stdinReader.stop();

    emit("DONE frames=" + std::to_string(framesRendered));
    return exitCode;
}
