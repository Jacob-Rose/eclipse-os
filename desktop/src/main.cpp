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

#include "edmx/config.h"
#include "edmx/dmx_output.h"
#include "edmx/fixture.h"
#include "edmx/pattern.h"
#include "edmx/serial_port.h"

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
            "  eclipse-dmx --list-patterns\n"
            "  eclipse-dmx --list-palettes\n"
            "\n"
            "options:\n"
            "  --config <file>     config file to load (required to run a show)\n"
            "  --dry-run           ignore device.type and print frames to stderr\n"
            "  --frames <n>        render n frames then exit (0 = run until stopped)\n"
            "  --port <path>       override device.port\n"
            "  --pattern <name>    override pattern.name\n"
            "  --fps <n>           override device.fps\n"
            "  --no-stdin          do not read the control protocol from stdin\n"
            "  --verbose           log every state change\n"
            "  --help              this text\n"
            "\n"
            "control protocol (one command per line on stdin):\n"
            "  pattern <name>            switch pattern\n"
            "  speed <float>             pattern speed\n"
            "  width <float>             pattern width\n"
            "  brightness <float>        pattern brightness (0..1)\n"
            "  master <float>            master brightness (0..1)\n"
            "  color <h> <s> <v>         colour for solid/pulse, h in degrees\n"
            "  palette <name|#hex,...>   named palette or an explicit stop list\n"
            "  blackout <on|off>         hold the rig dark without losing the look\n"
            "  status                    report current state\n"
            "  quit                      shut down, sending one dark frame first\n"
            "\n";
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
    };

    std::string describeState(const ShowState& show)
    {
        std::ostringstream out;
        out << "STATUS pattern=" << (show.pattern ? show.pattern->getName() : "none")
            << " speed=" << (show.pattern ? show.pattern->getSpeed() : 0.0f)
            << " width=" << (show.pattern ? show.pattern->getWidth() : 0.0f)
            << " brightness=" << (show.pattern ? show.pattern->getBrightness() : 0.0f)
            << " master=" << show.masterBrightness
            << " blackout=" << (show.blackout ? "on" : "off")
            << " fixtures=" << show.config.fixtures.size()
            << " fps=" << show.config.device.fps
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
            emit("OK pattern " + next.name);
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
    std::string patternOverride;
    float fpsOverride = 0.0f;
    long long frameLimit = 0;
    bool dryRun = false;
    bool useStdin = true;
    bool verbose = false;

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
        else if (arg == "--pattern")               patternOverride = nextArg("--pattern");
        else if (arg == "--fps")                   fpsOverride = std::stof(nextArg("--fps"));
        else if (arg == "--frames")                frameLimit = std::stoll(nextArg("--frames"));
        else if (arg == "--dry-run")               dryRun = true;
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

    if (!portOverride.empty())    show.config.device.port = portOverride;
    if (!patternOverride.empty()) show.config.pattern.name = patternOverride;
    if (fpsOverride > 0.0f)       show.config.device.fps = fpsOverride;
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

    if (!show.output->open(error))
    {
        logLine("output error: " + error);
        emit("ERR output " + error);
        return 1;
    }
    logLine("output: " + show.output->describe());

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
    show.context.positions.resize(fixtureCount);
    for (size_t idx = 0; idx < fixtureCount; ++idx)
    {
        show.context.positions[idx] = show.config.fixtures.normalizedPosition(idx);
    }

    emit("READY fixtures=" + std::to_string(fixtureCount)
       + " pattern=" + show.config.pattern.name
       + " output=" + show.output->describe());

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
    long long framesRendered = 0;
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

    stdinReader.stop();

    emit("DONE frames=" + std::to_string(framesRendered));
    return exitCode;
}
