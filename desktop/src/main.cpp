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
#include <cctype>
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
#include "lib/eio/relic.h"
#include "lib/elink/relic_link.h"

// The relic's own device layer, built for the host so --link-selftest can drive
// the code the sculpture actually runs. Nothing else here touches it.
#include "relics/obelisk/obelisk.h"

#include "edmx/beat_clock.h"
#include "edmx/beat_trigger.h"
#include "edmx/config.h"
#include "edmx/dmx_output.h"
#include "edmx/fixture.h"
#if !defined(_WIN32)
#  include "edmx/ftdi_dmx.h"
#endif
#include "edmx/midi_input.h"
#include "edmx/midi_output.h"
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
            "  eclipse-dmx --link-selftest\n"
            "  eclipse-dmx --probe-relics\n"
            "  eclipse-dmx --reboot-bootsel [--port <path>]\n"
            "  eclipse-dmx --list-patterns\n"
            "  eclipse-dmx --list-palettes\n"
            "  eclipse-dmx --list-profiles\n"
            "\n"
            "options:\n"
            "  --config <file>     config file to load (required to run a show)\n"
            "  --dry-run           ignore device.type and print frames to stderr\n"
            "  --frames <n>        render n frames then exit (0 = run until stopped)\n"
            "  --port <path>       override device.port\n"
            "  --device <type>     override device.type (enttec_pro, enttec_open,\n"
            "                      relic_usb, relic_usb_cue, console, preview)\n"
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
            "  beat                      a beat, now - tap the tempo in, or trigger a cue\n"
            "  midi list                 MIDI inputs the machine can see\n"
            "  midi open <spec>          follow tempo from that input\n"
            "  midi close                stop following, keep the tempo\n"
            "  midi align                the one is now - where the bar starts, for the\n"
            "                            slow rates and for a clock with no start\n"
            "  midi free-run <on|off>    keep pulsing when the clock stops\n"
            "  midi monitor <on|off>     print every message arriving, to identify a mapping\n"
            "  midi out <spec|close>     open a MIDI output to light a controller\n"
            "  midi send <hex> [hex...]  raw bytes down it, e.g. `midi send 90 0B 05`\n"
            "  midi status               port, tempo, lock\n"
            "  link pixels               drive the relic's LEDs from here\n"
            "  link cue                  give them back; the relic runs its own looks\n"
            "  link release              hand the pixels back now, staying in pixel mode\n"
            "  link cmd <text>           send a line to the relic's own handleCommand\n"
            "  link hello                ask the relic what it is\n"
            "  link bootsel              reboot the relic for reflashing; it does not come back\n"
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

    /// A LinkTransport with a queue behind it, so the relic's end of the cable
    /// can be driven with no cable.
    class LoopbackTransport : public elink::LinkTransport
    {
    public:
        int readByte() override
        {
            if (at >= incoming.size())
            {
                return -1;
            }
            return incoming[at++];
        }

        void writeLine(const char* text) override { said.emplace_back(text); }

        void feed(const uint8_t* data, size_t length)
        {
            incoming.insert(incoming.end(), data, data + length);
        }

        void feedByte(uint8_t value) { incoming.push_back(value); }

        std::vector<uint8_t> incoming;
        size_t at{0};
        std::vector<std::string> said;
    };

    /// A relic with one strip and a cable going nowhere.
    ///
    /// The strip is heap-owned by the RelicIO because that is how a real relic
    /// holds it, and holding it any other way here would test a different
    /// object lifetime than the one that ships.
    struct FakeRelic
    {
        explicit FakeRelic(uint16_t pixels)
        {
            std::unique_ptr<eio::HSVStrip> owned(new eio::HSVStrip(pixels, 0));
            strip = owned.get();
            io.strips.emplace(uint8_t{0}, std::move(owned));
            link.setTransport(&wire);
        }

        eio::RelicIO io;
        eio::HSVStrip* strip{nullptr};
        LoopbackTransport wire;
        elink::RelicLink link;
    };

    /// Drives the relic's end of the link with a synthesised desk and checks
    /// what lands on the strip. Returns 0 on success.
    ///
    /// This is the firmware, running on a laptop. `elink::FrameReader` and
    /// `elink::RelicLink` are the same translation units the sculpture will
    /// execute, and `eio::HSVStrip` is the same framebuffer — so the resync, the
    /// checksum, the takeover and the handback are all tested here before
    /// anything is flashed. What it cannot cover is the USB stack underneath and
    /// the timing of a real WS2812 write.
    ///
    /// Synthetic time throughout: RelicLink::tick takes its delta rather than
    /// reading a clock, so a holdover expiring is a number, not a wait.
    int runLinkSelfTest()
    {
        int failures = 0;

        const auto check = [&failures](const char* what, long long got, long long expected) {
            std::ostringstream line;
            const bool ok = (got == expected);
            line << (ok ? "SELFTEST ok   " : "SELFTEST FAIL ") << what
                 << " got=" << got << " expected=" << expected;
            emit(line.str());
            if (!ok)
            {
                ++failures;
            }
        };

        // A relic with one 8-pixel strip. Small enough to state every expected
        // byte, which is the point: a 344-pixel check that fails tells you
        // nothing about where.
        constexpr uint16_t kPixels = 8;

        const auto buildPixelFrame = [](uint16_t start, uint16_t count,
                                        uint8_t seed, std::vector<uint8_t>& out) {
            std::vector<uint8_t> payload(elink::PIXEL_HEADER_SIZE + count * 3u);
            elink::PixelHeader header;
            header.strip = 0;
            header.start = start;
            header.count = count;
            elink::writePixelHeader(header, payload.data());
            for (uint16_t i = 0; i < count * 3u; ++i)
            {
                payload[elink::PIXEL_HEADER_SIZE + i] = static_cast<uint8_t>(seed + i);
            }

            out.assign(elink::HEADER_SIZE + payload.size() + elink::TRAILER_SIZE, 0);
            const size_t written = elink::writeFrame(elink::FrameType::Pixels,
                                                     payload.data(),
                                                     static_cast<uint16_t>(payload.size()),
                                                     out.data(), out.size());
            out.resize(written);
        };

        // ---- a clean pixel frame lands on the strip ------------------------
        {
            FakeRelic relic(kPixels);

            std::vector<uint8_t> frame;
            buildPixelFrame(0, kPixels, 10, frame);
            relic.wire.feed(frame.data(), frame.size());

            relic.link.tick(0.025f, &relic.io);

            check("pixels: frame accepted", relic.link.getReader().framesAccepted(), 1);
            check("pixels: none rejected", relic.link.getReader().framesRejected(), 0);
            check("pixels: link owns the strip", relic.link.ownsPixels() ? 1 : 0, 1);

            const std::vector<uint8_t>& lit = relic.strip->getHostPixels();
            check("pixels: buffer sized", static_cast<long long>(lit.size()), kPixels * 3);

            int wrong = 0;
            for (uint16_t i = 0; i < kPixels * 3u; ++i)
            {
                if (lit[i] != static_cast<uint8_t>(10 + i))
                {
                    ++wrong;
                }
            }
            check("pixels: every byte is what was sent", wrong, 0);

            // The relic's own framebuffer must be untouched: it is what its
            // patterns read, and it should still hold the look underneath.
            check("pixels: strip_HSV left alone",
                  relic.strip->getStripHSV()[0].getValAs8(), 0);

        }

        // ---- garbage in front of a frame is discarded, not fatal ------------
        {
            FakeRelic relic(kPixels);

            // The tail of a previous frame, a lone magic byte, a doubled magic
            // byte - the three shapes a reconnect actually produces.
            const uint8_t junk[] = {0x00, 0xFF, 0xEC, 0x99, 0xEC, 0xEC, 0x41, 0x42};
            relic.wire.feed(junk, sizeof(junk));

            std::vector<uint8_t> frame;
            buildPixelFrame(0, kPixels, 200, frame);
            relic.wire.feed(frame.data(), frame.size());

            relic.link.tick(0.025f, &relic.io);

            check("resync: frame still accepted", relic.link.getReader().framesAccepted(), 1);
            check("resync: nothing rejected", relic.link.getReader().framesRejected(), 0);
            check("resync: first pixel correct", relic.strip->getHostPixels()[0], 200);

        }

        // ---- a corrupted byte is caught by the checksum ---------------------
        {
            FakeRelic relic(kPixels);

            std::vector<uint8_t> frame;
            buildPixelFrame(0, kPixels, 77, frame);
            frame[elink::HEADER_SIZE + elink::PIXEL_HEADER_SIZE + 2] ^= 0x40;
            relic.wire.feed(frame.data(), frame.size());

            relic.link.tick(0.025f, &relic.io);

            check("crc: rejected", relic.link.getReader().framesRejected(), 1);
            check("crc: not accepted", relic.link.getReader().framesAccepted(), 0);
            check("crc: strip untouched", relic.link.ownsPixels() ? 1 : 0, 0);

        }

        // ---- a bad length does not wedge the reader -------------------------
        {
            elink::FrameReader reader;
            const uint8_t bogus[] = {elink::MAGIC_0, elink::MAGIC_1, 0x01, 0xFF, 0xFF};
            for (uint8_t byte : bogus)
            {
                reader.push(byte);
            }

            check("length: absurd length rejected", reader.framesRejected(), 1);
            check("length: reader is idle again", reader.isIdle() ? 1 : 0, 1);

            // And it takes the very next frame.
            std::vector<uint8_t> frame;
            buildPixelFrame(0, 2, 1, frame);
            int accepted = 0;
            for (uint8_t byte : frame)
            {
                if (reader.push(byte))
                {
                    ++accepted;
                }
            }
            check("length: recovers immediately", accepted, 1);
        }

        // ---- the holdover hands the pixels back -----------------------------
        {
            FakeRelic relic(kPixels);
            relic.link.setHoldover(0.5f);

            std::vector<uint8_t> frame;
            buildPixelFrame(0, kPixels, 5, frame);
            relic.wire.feed(frame.data(), frame.size());
            relic.link.tick(0.025f, &relic.io);
            check("holdover: taken", relic.link.ownsPixels() ? 1 : 0, 1);

            // Just under, still ours.
            for (int i = 0; i < 18; ++i)
            {
                relic.link.tick(0.025f, &relic.io);
            }
            check("holdover: held at 0.475s", relic.link.ownsPixels() ? 1 : 0, 1);

            // And over.
            relic.link.tick(0.025f, &relic.io);
            relic.link.tick(0.025f, &relic.io);
            check("holdover: released", relic.link.ownsPixels() ? 1 : 0, 0);

        }

        // ---- Release hands them back at once --------------------------------
        {
            FakeRelic relic(kPixels);

            std::vector<uint8_t> frame;
            buildPixelFrame(0, kPixels, 5, frame);
            relic.wire.feed(frame.data(), frame.size());
            relic.link.tick(0.025f, &relic.io);

            uint8_t releaseFrame[elink::HEADER_SIZE + elink::TRAILER_SIZE];
            const size_t written = elink::writeFrame(elink::FrameType::Release, nullptr, 0,
                                                     releaseFrame, sizeof(releaseFrame));
            relic.wire.feed(releaseFrame, written);
            relic.link.tick(0.025f, &relic.io);

            check("release: given back", relic.link.ownsPixels() ? 1 : 0, 0);

        }

        // ---- commands arrive as text ----------------------------------------
        {
            elink::RelicLink link;
            LoopbackTransport wire;
            link.setTransport(&wire);

            const std::string text = "state theater";
            std::vector<uint8_t> frame(elink::HEADER_SIZE + text.size() + elink::TRAILER_SIZE);
            const size_t written = elink::writeFrame(
                elink::FrameType::Command,
                reinterpret_cast<const uint8_t*>(text.data()),
                static_cast<uint16_t>(text.size()),
                frame.data(), frame.size());
            wire.feed(frame.data(), written);

            link.tick(0.025f, nullptr);

            std::string got;
            check("command: one queued", link.takeCommand(got) ? 1 : 0, 1);
            check("command: text intact", got == text ? 1 : 0, 1);
            check("command: queue empty after", link.takeCommand(got) ? 1 : 0, 0);
        }

        // ---- a human at a serial monitor still works ------------------------
        {
            elink::RelicLink link;
            LoopbackTransport wire;
            link.setTransport(&wire);

            const char* typed = "switch\n";
            wire.feed(reinterpret_cast<const uint8_t*>(typed), 7);
            link.tick(0.025f, nullptr);

            std::string got;
            check("typed: line became a command", link.takeCommand(got) ? 1 : 0, 1);
            check("typed: text intact", got == "switch" ? 1 : 0, 1);
        }

        // ---- a frame the desk sized for a longer relic is clipped ------------
        {
            FakeRelic relic(kPixels);

            std::vector<uint8_t> frame;
            buildPixelFrame(0, kPixels * 2, 1, frame);
            relic.wire.feed(frame.data(), frame.size());
            relic.link.tick(0.025f, &relic.io);

            check("clip: frame accepted", relic.link.getReader().framesAccepted(), 1);
            check("clip: took what fits", static_cast<long long>(relic.strip->getHostPixels().size()),
                  kPixels * 3);
            check("clip: first pixel correct", relic.strip->getHostPixels()[0], 1);

        }

        // ---- a frame for a strip this relic does not have -------------------
        {
            FakeRelic relic(kPixels);

            std::vector<uint8_t> payload(elink::PIXEL_HEADER_SIZE + 3);
            elink::PixelHeader header;
            header.strip = 7;
            header.start = 0;
            header.count = 1;
            elink::writePixelHeader(header, payload.data());

            std::vector<uint8_t> frame(elink::HEADER_SIZE + payload.size() + elink::TRAILER_SIZE);
            const size_t written = elink::writeFrame(elink::FrameType::Pixels,
                                                     payload.data(),
                                                     static_cast<uint16_t>(payload.size()),
                                                     frame.data(), frame.size());
            relic.wire.feed(frame.data(), written);
            relic.link.tick(0.025f, &relic.io);

            check("route: counted as unrouted", relic.link.framesUnrouted(), 1);
            // Still a takeover: the desk is clearly driving, it is just aimed
            // wrong, and going dark would hide that.
            check("route: still a takeover", relic.link.ownsPixels() ? 1 : 0, 1);

        }

        // ---- a byte-at-a-time trickle is the same as a burst -----------------
        {
            FakeRelic relic(kPixels);

            std::vector<uint8_t> frame;
            buildPixelFrame(0, kPixels, 42, frame);

            // One byte per tick, which is what a slow link actually looks like.
            for (uint8_t byte : frame)
            {
                relic.wire.feedByte(byte);
                relic.link.tick(0.001f, &relic.io);
            }

            check("trickle: accepted", relic.link.getReader().framesAccepted(), 1);
            check("trickle: first pixel correct", relic.strip->getHostPixels()[0], 42);

        }

        // ---- the obelisk's own frame size round-trips ------------------------
        {
            constexpr uint16_t kObelisk = 344;
            FakeRelic relic(kObelisk);

            std::vector<uint8_t> frame;
            buildPixelFrame(0, kObelisk, 3, frame);
            // 5 header + 5 pixel header + 1032 pixel bytes + 2 crc
            check("obelisk: frame is 1044 bytes on the wire",
                  static_cast<long long>(frame.size()),
                  elink::HEADER_SIZE + elink::PIXEL_HEADER_SIZE + kObelisk * 3 + elink::TRAILER_SIZE);

            relic.wire.feed(frame.data(), frame.size());
            relic.link.tick(0.025f, &relic.io);

            check("obelisk: accepted", relic.link.getReader().framesAccepted(), 1);

            int wrong = 0;
            for (uint16_t i = 0; i < kObelisk * 3u; ++i)
            {
                if (relic.strip->getHostPixels()[i] != static_cast<uint8_t>(3 + i))
                {
                    ++wrong;
                }
            }
            check("obelisk: all 1032 bytes correct", wrong, 0);

        }

        // ---- the sculpture's own main loop ----------------------------------
        //
        // Everything above tests the link in isolation. This runs the real
        // ObeliskCore - its geometry, its state machine, RelicCore::runTick and
        // the gating inside it - which is the code the Pico will execute, minus
        // the neopixel driver and the USB stack under it.
        {
            obelisk::ObeliskCore core;
            LoopbackTransport wire;
            core.getLink().setTransport(&wire);
            core.getLink().setIdentity("obelisk");

            // Free-running first: its own look, on its own pixels.
            core.runTick();
            core.runTick();
            check("firmware: runs its own look unlinked", core.getLink().ownsPixels() ? 1 : 0, 0);

            // Now the desk takes it.
            constexpr uint16_t kObelisk = 344;
            std::vector<uint8_t> frame;
            buildPixelFrame(0, kObelisk, 90, frame);
            wire.feed(frame.data(), frame.size());
            core.runTick();

            check("firmware: desk took the pixels", core.getLink().ownsPixels() ? 1 : 0, 1);

            // Reaching into the relic the way the link does, to read back what
            // would have gone to the LEDs.
            const eio::HSVStrip* strip = nullptr;
            if (core.getIO())
            {
                auto found = core.getIO()->strips.find(0);
                if (found != core.getIO()->strips.end())
                {
                    strip = found->second.get();
                }
            }
            check("firmware: strip found", strip != nullptr ? 1 : 0, 1);
            if (strip)
            {
                check("firmware: obelisk is 344 pixels", strip->getLength(), kObelisk);

                int wrong = 0;
                for (uint16_t i = 0; i < kObelisk * 3u; ++i)
                {
                    if (strip->getHostPixels()[i] != static_cast<uint8_t>(90 + i))
                    {
                        ++wrong;
                    }
                }
                check("firmware: the desk's frame is what is lit", wrong, 0);
            }

            // A cue reaches handleCommand even mid-stream.
            const std::string cue = "state theater";
            std::vector<uint8_t> cueFrame(elink::HEADER_SIZE + cue.size() + elink::TRAILER_SIZE);
            const size_t written = elink::writeFrame(
                elink::FrameType::Command,
                reinterpret_cast<const uint8_t*>(cue.data()),
                static_cast<uint16_t>(cue.size()),
                cueFrame.data(), cueFrame.size());
            wire.feed(cueFrame.data(), written);
            core.runTick();

            int acknowledged = 0;
            for (const std::string& said : wire.said)
            {
                if (said == "EOSLINK state theater")
                {
                    ++acknowledged;
                }
            }
            check("firmware: cue reached the relic mid-stream", acknowledged, 1);

            // And the handback, asked for rather than timed out - runTick takes
            // its delta from a real clock, so a holdover here would mean
            // actually waiting. The timeout path is covered above, in synthetic
            // time; what this checks is that RelicCore goes back to rendering
            // its own look once the link lets go.
            uint8_t releaseFrame[elink::HEADER_SIZE + elink::TRAILER_SIZE];
            const size_t releaseSize = elink::writeFrame(elink::FrameType::Release, nullptr, 0,
                                                         releaseFrame, sizeof(releaseFrame));
            wire.feed(releaseFrame, releaseSize);
            core.runTick();

            check("firmware: pixels handed back", core.getLink().ownsPixels() ? 1 : 0, 0);

            // The proof it is rendering again: its own look writes through
            // strip_HSV, which the streamed frames deliberately never touch.
            core.runTick();
            core.runTick();
            if (strip)
            {
                int lit = 0;
                for (const ecore::HSV& colour : const_cast<eio::HSVStrip*>(strip)->getStripHSV())
                {
                    if (colour.getValAs8() > 0)
                    {
                        ++lit;
                    }
                }
                check("firmware: its own look is running again", lit > 0 ? 1 : 0, 1);
            }
        }

        emit(failures == 0 ? "SELFTEST PASS" : "SELFTEST FAILURES " + std::to_string(failures));
        return failures == 0 ? 0 : 1;
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

        // ---- a stream that duplicates, drops and strays ---------------------
        //
        // The three things a real Mixxx cable does that a synthetic one does
        // not, and the reason the clock counts beats rather than messages. What
        // is being checked is not the tempo - that survives all of this on its
        // own - but the *count*, because that is what half time and once-a-bar
        // are counted off. One extra or one missing and the look moves onto
        // another beat of the bar and stays there.
        {
            BeatClock clock;
            MidiInput midi;
            midi.setBeatClock(&clock);
            midi.setBpmNote(-1);

            constexpr double kBpm = 128.0;
            constexpr double kPeriod = 60.0 / kBpm;
            constexpr int kBeats = 64;

            clock.setBpm(static_cast<float>(kBpm), BeatSource::Internal, 0.0);

            const double one = 10.0;
            clock.restart(one, BeatSource::MidiNote); // the one is here

            // Where the bar was, on every beat of the run. Sampled as we go
            // rather than at the end, because the clock is a live grid and not
            // a history: it can only answer about the beat it is on.
            int wrongBar = 0;

            for (int beat = 1; beat <= kBeats; ++beat)
            {
                const double at = one + (beat * kPeriod);

                if ((beat % 7) != 0) // ...and when it is, the message never arrives
                {
                    midi.handleMessage(0x90, 50, 100, at);

                    if ((beat % 5) == 0)
                    {
                        // The same beat, said twice, 3ms apart.
                        midi.handleMessage(0x90, 50, 100, at + 0.003);
                    }
                    if ((beat % 11) == 0)
                    {
                        // The other deck, half a beat out of step with this one.
                        midi.handleMessage(0x90, 50, 100, at + (kPeriod * 0.5));
                    }
                }

                if (clock.beatInBar(at + (kPeriod * 0.25)) != (beat % BeatClock::kBeatsPerBar))
                {
                    ++wrongBar;
                }
            }

            const double end = one + (kBeats * kPeriod) + (kPeriod * 0.25);
            check("grid.beats", static_cast<double>(clock.beatsSinceDownbeat(end)),
                  kBeats, 0.0);
            check("grid.bar_beat", static_cast<double>(clock.beatInBar(end)),
                  kBeats % BeatClock::kBeatsPerBar, 0.0);
            check("grid.bpm", clock.getBpm(), kBpm, 1.0);

            // The one is the one for all 64 of them, which is the whole point:
            // a look on quarter time hits 16 times and every one of them is a
            // bar apart.
            check("grid.bar_holds", static_cast<double>(wrongBar), 0.0, 0.0);
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

        // ---- the VU meter ---------------------------------------------------
        {
            BeatClock clock;
            AudioLevel meter;
            MidiInput midi;
            midi.setBeatClock(&clock);
            midi.setAudioLevel(&meter);

            // All three arrive interleaved on the same channel and must land in
            // three separate places. Mixing them up is not cosmetic: a backdrop
            // that gets the instantaneous level instead of the average pulses
            // on every kick, which is exactly the bug this pins down.
            midi.handleMessage(0x90, 64, 127, 30.0); // instantaneous, loud
            midi.handleMessage(0x90, 68, 64, 30.0);  // average, half
            midi.handleMessage(0x90, 69, 32, 30.0);  // meter bar, quarter

            check("vu.instant", meter.get(VuSource::Instant, 30.0), 1.0, 0.001);
            check("vu.average", meter.get(VuSource::Average, 30.0), 64.0 / 127.0, 0.001);
            check("vu.meter", meter.get(VuSource::Meter, 30.0), 32.0 / 127.0, 0.001);

            // Held flat, not decaying from the instant of the reading. A
            // backdrop tracking this every frame would otherwise ripple.
            check("vu.holds_between_messages",
                  meter.get(VuSource::Average, 30.0 + AudioLevel::kHoldFor - 0.05),
                  64.0 / 127.0, 0.001);

            // They arrive on the same channel as the beat, dozens of times a
            // second. Taking one for a beat would not be a subtle failure.
            check("vu.not_a_beat", static_cast<double>(midi.getBeats()), 0.0, 0.0);

            const double stale = 30.0 + AudioLevel::kStaleAfter + 0.1;
            check("vu.live", meter.isLive(VuSource::Average, 30.1) ? 1.0 : 0.0, 1.0, 0.0);
            check("vu.decays_when_stale", meter.get(VuSource::Average, stale), 0.0, 0.0);
            check("vu.dead_when_stale",
                  meter.isAnyLive(stale) ? 1.0 : 0.0, 0.0, 0.0);

            // A source nobody is sending stays at zero rather than inheriting
            // from a neighbour.
            AudioLevel untouched;
            check("vu.unfed_is_zero", untouched.get(VuSource::Instant, 30.0), 0.0, 0.0);
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

    /// Everything from `first` on, back as one space-separated string.
    ///
    /// Port names have spaces in them - "Launchpad X LPX MIDI Out" is four
    /// words - and a spec taken as `words[2]` is the word "Launchpad", which
    /// then matches two ports and refuses. Anything whose argument is a name
    /// rather than a token wants this instead of an index into `words`.
    std::string joinFrom(const std::vector<std::string>& words, size_t first)
    {
        std::string joined;
        for (size_t index = first; index < words.size(); ++index)
        {
            if (!joined.empty())
            {
                joined += ' ';
            }
            joined += words[index];
        }
        return joined;
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

        /// True means the reader is still parked in getline and the caller
        /// must not let normal process teardown run - see the exit note where
        /// this is called.
        bool stop()
        {
            running = false;
            if (!thread.joinable())
            {
                return false;
            }
            if (eof)
            {
                // getline already returned; the thread is on its way out and
                // the join is immediate.
                thread.join();
                return false;
            }
            // std::cin has no portable interrupt, so we leave the reader
            // detached rather than hang the shutdown waiting on a line
            // that is never going to arrive.
            thread.detach();
            return true;
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
    /// One device, running.
    ///
    /// Everything here is per-device because a room can hold a sculpture on a
    /// USB cable and a truss on a DMX widget at once, and they agree on
    /// precisely one thing: the pattern. Frame buffers, wires, refresh rates
    /// and channel counts are all their own.
    /// Is there an FTDI on the USB bus that an Open DMX output could drive?
    ///
    /// Asked because "no serial port found" stops being the right answer once
    /// libftdi is in play: it detaches ftdi_sio to reach the chip, and while it
    /// holds it there is no /dev/ttyUSBn to find. A widget that is plainly
    /// attached should not be reported missing on the strength of a device node
    /// that a driver took away on purpose.
    bool ftdiCanSeeAWidget()
    {
#if defined(_WIN32)
        return false;
#else
        const FtdiApi* api = FtdiApi::get();
        if (api == nullptr)
        {
            return false;
        }

        void* context = api->newContext();
        if (context == nullptr)
        {
            return false;
        }

        // Opening is the only honest test - a device that is there but claimed
        // by something else is not one we can drive either.
        const bool found = api->usbOpen(context, ftdi::kVendorFtdi, ftdi::kProductFt232) == 0;
        if (found)
        {
            api->usbClose(context);
        }
        api->freeContext(context);
        return found;
#endif
    }

    /// How often a device with no wire asks for one again.
    ///
    /// Two seconds because the cost is one open() that fails, and the thing
    /// being waited for is a human plugging a cable in. Fast enough that it
    /// feels immediate, slow enough that a relic probe - which opens every
    /// serial port on the machine and waits on each - is not running constantly
    /// behind a show.
    constexpr double kDeviceRetrySeconds = 2.0;

    struct DeviceRuntime
    {
        const Device* config{nullptr};
        std::unique_ptr<DmxOutput> output;
        std::unique_ptr<eio::HSVStrip> strip;
        DmxUniverse universe;

        /// Where this device's fixtures sit in the show-wide run, which is the
        /// order the pattern renders in and the frame stream reports.
        size_t firstFixture{0};
        size_t fixtureCount{0};

        /// Devices do not share a refresh rate - a relic draws at 30 and a DMX
        /// widget at 40 - so each one sends when it is due rather than every
        /// device sending on the fastest one's schedule.
        float fps{40.0f};
        double nextSendAt{0.0};

        /// Why this device has no wire, or empty when it has one.
        ///
        /// A rig that is not plugged in must not stop the show. Half of what
        /// this program is for is building a look before the truss exists, and
        /// an environment names every device in the room whether or not today's
        /// bench has all of them on it. So a device whose widget is missing
        /// stays in the show and keeps rendering - it appears in the frame
        /// stream, the viewer draws it, the pattern spans it - it simply has
        /// nowhere to send. Loudly: see where this is set.
        ///
        /// Note what is *not* covered by this. A config naming a device file
        /// that does not exist is still fatal, because that is a typo rather
        /// than an absent rig, and carrying on would light a room that is
        /// missing the thing the show is about.
        std::string offline;

        /// When to try giving this device its wire again; 0 until the frame
        /// loop has seen it offline and picked a time.
        ///
        /// Startup is not the only moment a widget can appear. It gets plugged
        /// in late, it gets knocked out and put back, its device node is
        /// recreated by udev with permissions that then get fixed - and every
        /// one of those is a show that should light when the cable arrives
        /// rather than one that needs restarting at the venue with the room
        /// already full. So an offline device keeps asking.
        double nextRetryAt{0.0};

        /// What we last said about why it is offline, so the retry loop can
        /// stay quiet while the answer is not changing. A widget that is simply
        /// not plugged in yet would otherwise print a line a second all night.
        std::string reportedOffline;

        bool isLive() const { return output != nullptr; }

        /// Where this device's frames go, for a status line or a UI.
        std::string describeOutput() const
        {
            return output ? output->describe() : ("offline: " + offline);
        }

        const std::string& name() const { return config->name; }
    };

    struct ShowState
    {
        Config config;
        std::unique_ptr<Pattern> pattern;
        std::vector<DeviceRuntime> devices;
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

        /// Lamps out, when there is a controller to light. Nothing in here
        /// knows what a pad means - see midi_output.h: the desk decides which
        /// bytes, this only carries them. Independent of `midi` on purpose,
        /// because they are usually not the same port and a controller with no
        /// lamps must not cost us the beat.
        MidiOutput midiOut;

        /// The last accepted `state ...` line, verbatim - blend seconds and
        /// all. Replayed to a cue-mode relic the moment it comes online, so a
        /// sculpture plugged in mid-show joins the show at its current look
        /// instead of sitting in its own idle until the next transition.
        std::string lastCue;

        /// A second pattern on a few named fixtures, rendered over the show
        /// each frame - the UV par with its own off / flash / on machine
        /// while the truss around it follows the scanner. Its pattern sees
        /// only its fixtures, as a rig of their own that still carries the
        /// stage coordinates and spaces those fixtures have in the show.
        /// See LayerConfig, buildLayers and the `layer` command.
        struct Layer
        {
            const LayerConfig* config{nullptr};
            std::string name;
            std::vector<size_t> fixtures;   ///< show-wide indices, in the layer's order
            std::unique_ptr<Pattern> pattern;
            PatternContext context;
            std::vector<ecore::HSV> colors;
        };
        std::vector<Layer> layers;
    };

    /// Each layer's slice of the rig, taken from the show's own context -
    /// re-cut whenever that is, so a placement change reaches the layers.
    void applyLayerContexts(ShowState& show)
    {
        for (ShowState::Layer& layer : show.layers)
        {
            PatternContext& context = layer.context;
            const size_t count = layer.fixtures.size();

            context.fixtureCount = count;
            context.positions.resize(count);
            context.nodeCoords.resize(count);
            context.nodeMappings.resize(count);

            for (size_t idx = 0; idx < count; ++idx)
            {
                const size_t at = layer.fixtures[idx];
                context.positions[idx] = (count > 1)
                    ? static_cast<float>(idx) / static_cast<float>(count - 1) : 0.0f;
                if (at < show.context.nodeCoords.size())
                {
                    context.nodeCoords[idx] = show.context.nodeCoords[at];
                }
                if (at < show.context.nodeMappings.size())
                {
                    context.nodeMappings[idx] = show.context.nodeMappings[at];
                }
            }

            if (layer.pattern)
            {
                context.coords = layer.pattern->defaultCoordFrame();
            }
            layer.colors.assign(count, ecore::HSV());
        }
    }

    /// Resolves the config's layers against the devices and builds their
    /// patterns.
    ///
    /// A fixture name that resolves to nothing is fatal, the way a device
    /// file that does not exist is: it is a typo, and a layer quietly bound
    /// to nothing would be a UV that never comes on with no message saying
    /// why. `device/fixture` names one exactly; a bare `fixture` is accepted
    /// while only one device has it.
    bool buildLayers(ShowState& show, std::string& outError)
    {
        show.layers.clear();

        for (const LayerConfig& config : show.config.layers)
        {
            ShowState::Layer layer;
            layer.config = &config;
            layer.name = config.name;

            for (const std::string& reference : config.fixtures)
            {
                const size_t slash = reference.find('/');
                const std::string deviceName = (slash == std::string::npos) ? "" : reference.substr(0, slash);
                const std::string fixtureName = (slash == std::string::npos) ? reference : reference.substr(slash + 1);

                std::vector<size_t> found;
                size_t at = 0;
                for (const Device& device : show.config.devices)
                {
                    for (size_t idx = 0; idx < device.fixtures.size(); ++idx)
                    {
                        if ((deviceName.empty() || device.name == deviceName)
                            && device.fixtures[idx].name == fixtureName)
                        {
                            found.push_back(at + idx);
                        }
                    }
                    at += device.fixtures.size();
                }

                if (found.empty())
                {
                    outError = "layer '" + config.name + "': no fixture named '" + reference + "'";
                    return false;
                }
                if (found.size() > 1 && deviceName.empty())
                {
                    outError = "layer '" + config.name + "': more than one device has a '" + reference
                             + "'; name it as device/" + reference;
                    return false;
                }
                layer.fixtures.insert(layer.fixtures.end(), found.begin(), found.end());
            }

            PatternConfig patternConfig;
            patternConfig.name = config.pattern;
            patternConfig.stateName = config.state;
            layer.pattern = makePattern(config.pattern, patternConfig, outError);
            if (!layer.pattern)
            {
                outError = "layer '" + config.name + "': " + outError;
                return false;
            }

            show.layers.push_back(std::move(layer));
        }

        applyLayerContexts(show);
        return true;
    }

    ShowState::Layer* findLayer(ShowState& show, const std::string& name)
    {
        for (ShowState::Layer& layer : show.layers)
        {
            if (layer.name == name)
            {
                return &layer;
            }
        }
        return nullptr;
    }

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

        // ---- and where every node ends up in it -------------------------
        //
        // With more than one device this is the whole ballgame: the pattern
        // renders into one space, and each device's placement decides which
        // part of that space it occupies. Get it wrong and two rigs run the
        // same look twice from scratch instead of one look across both.
        //
        // Local coordinates first, in whatever the device's own space is:
        //
        //   literal     the numbers as written - a relic describing its own
        //               geometry, where the look was tuned against the object
        //   normalized  0..1 along the rig, stretched across the pattern's
        //               coordinate frame, which is what a truss of pars wants
        //
        // then `world = local * scale + offset` from the placement. Filled for
        // every device, always: with one device at the default placement this
        // is exactly what the single-rig path produced before.
        show.context.nodeCoords.resize(show.context.fixtureCount);
        show.context.nodeMappings.resize(show.context.fixtureCount);

        size_t at = 0;
        for (const Device& device : show.config.devices)
        {
            const size_t count = device.fixtures.size();
            const eio::NodeSpace space = eio::nodeSpaceFromName(device.space);

            // Pass one: local coordinates, and how far they reach.
            float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
            for (size_t idx = 0; idx < count; ++idx)
            {
                ecore::Coordinate local;

                if (device.coordSpace == CoordSpace::Literal)
                {
                    local.x = device.fixtures[idx].positionX;
                    local.y = device.fixtures[idx].positionY;
                }
                else
                {
                    local = show.context.coords.at(device.fixtures.normalizedPosition(idx));
                }

                if (idx == 0)
                {
                    minX = maxX = local.x;
                    minY = maxY = local.y;
                }
                else
                {
                    minX = std::min(minX, local.x); maxX = std::max(maxX, local.x);
                    minY = std::min(minY, local.y); maxY = std::max(maxY, local.y);
                }

                show.context.nodeCoords[at + idx] = local;
            }

            // The relative view, taken before the placement rewrites the
            // locals: the device's own coordinates, and the same normalized
            // over its extent. An axis with no extent - a flat row of pars -
            // reads 0.5, the middle, rather than dividing by nothing.
            {
                const float extentX = maxX - minX;
                const float extentY = maxY - minY;
                for (size_t idx = 0; idx < count; ++idx)
                {
                    const ecore::Coordinate& local = show.context.nodeCoords[at + idx];
                    PatternContext::NodeMapping& mapping = show.context.nodeMappings[at + idx];
                    mapping.space = space;
                    mapping.index = static_cast<int>(idx);
                    mapping.count = static_cast<int>(count);
                    mapping.local = local;
                    mapping.u = (extentX > 1e-6f) ? (local.x - minX) / extentX : 0.5f;
                    mapping.v = (extentY > 1e-6f) ? (local.y - minY) / extentY : 0.5f;
                }
            }

            // Pass two: the placement, now that the extent is known. `fit`
            // derives a scale from it and re-bases the corner to the offset;
            // without it, scale and offset are used as written.
            Placement resolved = device.placement;
            float baseX = 0.0f;
            float baseY = 0.0f;

            if (device.placement.fitWidth)
            {
                const float extent = maxX - minX;
                resolved.scaleX = (extent > 1e-6f) ? (*device.placement.fitWidth / extent) : 0.0f;
                baseX = minX;
            }
            if (device.placement.fitHeight)
            {
                const float extent = maxY - minY;
                resolved.scaleY = (extent > 1e-6f) ? (*device.placement.fitHeight / extent) : 0.0f;
                baseY = minY;
            }

            for (size_t idx = 0; idx < count; ++idx)
            {
                const ecore::Coordinate& local = show.context.nodeCoords[at + idx];
                show.context.nodeCoords[at + idx] =
                    resolved.apply(local.x - baseX, local.y - baseY);
            }

            at += count;
        }

        // the layers see the stage through the show's nodes, so they follow
        applyLayerContexts(show);
    }

    /// Tells a UI which states a pattern can offer, and which is showing.
    ///
    /// `prefix` is "" for the show's own pattern and "LAYER <name> " for a
    /// layer's, on every line - one announcement format, and a client that
    /// reads the show's reads a layer's by stripping the prefix.
    void emitStatesFor(Pattern* pattern, const std::string& prefix)
    {
        StateMachinePattern* machine = pattern ? pattern->asStateMachine() : nullptr;
        if (!machine)
        {
            emit(prefix + "STATES");
            return;
        }

        std::ostringstream out;
        out << "STATES";
        for (const std::string& state : machine->stateNames())
        {
            out << " " << state;
        }
        emit(prefix + out.str());
        emit(prefix + "STATE " + machine->currentStateName());
    }

    /// Emitted whenever the pattern changes, so a client that switches to a
    /// state machine learns its states without having to ask.
    void emitStates(ShowState& show)
    {
        emitStatesFor(show.pattern.get(), "");
    }

    /// A colour as `#rrggbb`, the way every other colour on this wire is
    /// written — the config file reads them, the F frames emit them, and a
    /// param is not the place to invent a fourth spelling.
    std::string colorToHex(const ecore::HSV& color)
    {
        const Rgb8 rgb = hsvToRgb8(color);
        char text[8];
        std::snprintf(text, sizeof(text), "#%02x%02x%02x", rgb.r, rgb.g, rgb.b);
        return std::string(text);
    }

    /// One `PARAM name <type> <value> <min> <max>` line.
    ///
    /// One function rather than one at each site, because a param is emitted
    /// from two of them — the whole set on a cue change, and the echo after a
    /// `param` — and a format that drifts between them is a client that works
    /// until it doesn't.
    std::string paramLine(const ecore::Property& property)
    {
        std::ostringstream out;
        out << "PARAM " << property.name << " ";

        switch (property.type)
        {
            case ecore::Property::Type::Bool:  out << "b " << property.get(); break;
            case ecore::Property::Type::Color: out << "c " << colorToHex(property.getColor()); break;
            case ecore::Property::Type::Float: out << "f " << property.get(); break;
        }

        out << " " << property.minValue << " " << property.maxValue;
        return out.str();
    }

    /// One `CURVE name t:v[:easing] ...` line: a look's live AutomationCurve.
    ///
    /// Easing travels as the easing_functions enum *index* - the python
    /// mirror's EASING_NAMES is in enum order precisely so an integer means
    /// the same shape on both sides. A key with no third field is linear.
    std::string curveLine(const eanim::CurveRef& ref)
    {
        std::ostringstream out;
        out << "CURVE " << ref.name;

        for (int idx = 0; idx < ref.curve->getKeyCount(); ++idx)
        {
            const eanim::AutomationKey* key = ref.curve->getKey(idx);
            out << " " << key->time << ":" << key->value;
            if (key->bUseEasing)
            {
                out << ":" << static_cast<int>(key->easingFunction);
            }
        }
        return out.str();
    }

    /// The tunable knobs of whatever is showing, for a UI to build controls from.
    ///
    /// Emitted whenever the pattern or the state changes, because on a state
    /// machine the knobs belong to the *look* — switching cue replaces the set
    /// wholesale. A client rebuilds on the PARAMS line and fills from the PARAM
    /// lines under it.
    ///
    ///   PARAMS mythos26 beat_pulse
    ///   PARAM attack f 0.2 0 1
    ///   PARAM hold b 1 0 1
    ///   PARAM color c #ffffff 0 1
    ///
    /// Bools and colours carry a range too, pointless as it is, so one parser
    /// reads all three and only branches on the widget it builds.
    void emitParamsFor(Pattern* pattern, const std::string& prefix)
    {
        StateMachinePattern* machine = pattern ? pattern->asStateMachine() : nullptr;

        std::ostringstream header;
        header << "PARAMS " << (pattern ? pattern->getName() : "none")
               << " " << (machine ? machine->currentStateName() : "-");
        emit(prefix + header.str());

        if (!pattern)
        {
            return;
        }

        ecore::PropertyBag bag;
        pattern->reflect(bag);

        for (const ecore::Property& property : bag.all())
        {
            emit(prefix + paramLine(property));
        }

        // The look's drawable shapes ride in the same block: a client that
        // rebuilds on PARAMS gets knobs and curves as one announcement, with
        // each curve's *current* keys - which is what lets a curve editor
        // open on the live envelope rather than on a blank.
        eanim::CurveBag curves;
        pattern->reflectCurves(curves);
        for (const eanim::CurveRef& ref : curves.all())
        {
            emit(prefix + curveLine(ref));
        }
    }

    void emitParams(ShowState& show)
    {
        emitParamsFor(show.pattern.get(), "");
    }

    /// The current set as one line of JSON, for keeping a tuning session.
    std::string dumpLine(Pattern& pattern)
    {
        ecore::PropertyBag bag;
        pattern.reflect(bag);

        std::ostringstream out;
        out << "DUMP {";
        bool first = true;
        for (const ecore::Property& property : bag.all())
        {
            out << (first ? "" : ", ") << "\"" << property.name << "\": ";
            if (property.type == ecore::Property::Type::Bool)
            {
                out << (property.get() != 0.0f ? "true" : "false");
            }
            else if (property.type == ecore::Property::Type::Color)
            {
                // Quoted, as a config file writes a colour - the point of a
                // dump is that it can be pasted back.
                out << "\"" << colorToHex(property.getColor()) << "\"";
            }
            else
            {
                out << property.get();
            }
            first = false;
        }
        out << "}";
        return out.str();
    }

    /// `param <name> <value>` against one pattern.
    ///
    /// Looks the knob up before reading the value, because what the value
    /// *is* depends on the knob: `#ff2200` is a colour on a colour and a
    /// mis-parse on an attack time. Says what it actually landed on - and
    /// every curve, since a knob can rebuild one - *before* the caller's OK,
    /// so a client that waits on the reply and then reads has the new value.
    /// False after an ERR has gone out.
    bool applyParam(Pattern& pattern, const std::string& name, const std::string& text,
                    const std::string& prefix)
    {
        ecore::PropertyBag bag;
        pattern.reflect(bag);

        const ecore::Property* target = bag.find(name);
        if (target == nullptr)
        {
            std::string known;
            for (const ecore::Property& property : bag.all())
            {
                known += (known.empty() ? "" : ", ") + property.name;
            }
            emit("ERR no param '" + name + "'"
               + (known.empty() ? " (this look has none)" : " (have: " + known + ")"));
            return false;
        }

        if (target->type == ecore::Property::Type::Color)
        {
            ecore::HSV parsed;
            if (!parseColorString(text, parsed))
            {
                emit("ERR param " + name + ": '" + text + "' is not a colour (want '#rrggbb')");
                return false;
            }
            bag.setColor(name, parsed);
        }
        else
        {
            float value = 0.0f;
            if (!parseFloatArg(text, value))
            {
                // A bool reads better as on/off at a desk than as 1/0, and
                // the two spellings cost one comparison each.
                if (text == "on" || text == "true")        value = 1.0f;
                else if (text == "off" || text == "false") value = 0.0f;
                else
                {
                    emit("ERR '" + text + "' is not a number");
                    return false;
                }
            }
            bag.set(name, value);
        }

        emit(prefix + paramLine(*target));

        eanim::CurveBag curves;
        pattern.reflectCurves(curves);
        for (const eanim::CurveRef& ref : curves.all())
        {
            emit(prefix + curveLine(ref));
        }
        return true;
    }

    /// `curve <name> t:v[:easing] ...` against one pattern: the whole shape
    /// at once, parsed into a scratch curve first so a typo cannot leave the
    /// look holding half of one. Echoes what the look now holds before the
    /// caller's OK. False after an ERR has gone out.
    bool applyCurve(Pattern& pattern, const std::string& name,
                    const std::vector<std::string>& words, size_t firstKey,
                    const std::string& prefix)
    {
        eanim::CurveBag bag;
        pattern.reflectCurves(bag);

        eanim::CurveRef* target = bag.find(name);
        if (target == nullptr)
        {
            std::string known;
            for (const eanim::CurveRef& ref : bag.all())
            {
                known += (known.empty() ? "" : ", ") + ref.name;
            }
            emit("ERR no curve '" + name + "'"
               + (known.empty() ? " (this look has none)" : " (have: " + known + ")"));
            return false;
        }

        eanim::AutomationCurve parsed;
        for (size_t at = firstKey; at < words.size(); ++at)
        {
            float keyTime = 0.0f;
            float keyValue = 0.0f;
            int easing = -1;
            const int got = std::sscanf(words[at].c_str(), "%f:%f:%d", &keyTime, &keyValue, &easing);
            if (got < 2)
            {
                emit("ERR curve key '" + words[at] + "' is not t:v or t:v:easing");
                return false;
            }
            if (got >= 3 && (easing < 0 || easing > static_cast<int>(easing_functions::EaseInOutBounce)))
            {
                emit("ERR curve key '" + words[at] + "': no easing #" + std::to_string(easing));
                return false;
            }

            const bool added = (got >= 3)
                ? parsed.addKey(keyTime, keyValue, static_cast<easing_functions>(easing))
                : parsed.addKey(keyTime, keyValue);
            if (!added)
            {
                emit("ERR curve holds at most " + std::to_string(eanim::AutomationCurve::kMaxKeys) + " keys");
                return false;
            }
        }

        *target->curve = parsed;
        if (target->onChanged)
        {
            target->onChanged();
        }

        emit(prefix + curveLine(*target));
        return true;
    }

    /// Everything a client needs to know about one layer, in the order a
    /// client that learns the show learns it: what it is on, then its
    /// states and knobs with the same lines the show gets, prefixed.
    void announceLayer(ShowState::Layer& layer)
    {
        const std::string prefix = "LAYER " + layer.name + " ";

        std::ostringstream out;
        out << "FIXTURES";
        for (size_t at : layer.fixtures)
        {
            out << " " << at;
        }
        emit(prefix + out.str());
        emit(prefix + "PATTERN " + (layer.pattern ? layer.pattern->getName() : "none"));

        emitStatesFor(layer.pattern.get(), prefix);
        emitParamsFor(layer.pattern.get(), prefix);
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
            // The ALSA sequencer address, where there is one. It is what
            // aconnect speaks, and `midi.port` takes it verbatim - which is the
            // only unambiguous answer when two clients pick the same name.
            const std::string address = port.address();

            emit("MIDI " + std::to_string(port.index) + "\t" + port.name
               + (address.empty() ? "" : ("\t" + address))
               + (MidiInput::isIgnored(port.name, ignore) ? "\tignored" : ""));
        }

        // Outputs too, on their own prefix. A separate list with its own
        // indices, because they are a separate namespace - "0" as an input is
        // not "0" as an output - and one list of both would invite exactly
        // that confusion at a load-in.
        const std::vector<MidiOutPortInfo> outs = MidiOutput::enumeratePorts();
        for (const MidiOutPortInfo& port : outs)
        {
            emit("MIDI-OUT " + std::to_string(port.index) + "\t" + port.name
               + "\t" + port.address);
        }

        emit("OK " + std::to_string(ports.size()) + " midi inputs, "
           + std::to_string(outs.size()) + " outputs");
    }

    /// Everything the config says about how to read the cable, in one place.
    ///
    /// Two callers — startup and `midi open` — and a filter applied in only one
    /// of them is a bug that presents as "it works from the config but not when
    /// I reopen the port", which is a miserable thing to chase at a venue.
    void applyMidiConfig(ShowState& show)
    {
        show.midi.setFollowClock(show.config.midi.followClock);
        show.midi.setFollowNotes(show.config.midi.followNotes);
        show.midi.setBeatNote(show.config.midi.beatNote);
        show.midi.setBpmNote(show.config.midi.bpmNote);
        show.midi.setVuNote(VuSource::Instant, show.config.midi.vuInstantNote);
        show.midi.setVuNote(VuSource::Average, show.config.midi.vuAverageNote);
        show.midi.setVuNote(VuSource::Meter, show.config.midi.vuMeterNote);
        show.midi.setBeatChannel(show.config.midi.beatChannel);
        show.midi.setAudioLevel(&sharedAudioLevel());
    }

    /// Every device's output, for a status line.
    std::string describeOutputs(const ShowState& show)
    {
        std::string out;
        for (const DeviceRuntime& device : show.devices)
        {
            out += (out.empty() ? "" : ", ") + device.name() + ":" + device.describeOutput();
        }
        return out.empty() ? "none" : out;
    }

    /// The relics on the other end of this show's cables.
    ///
    /// A `link` command applies to all of them. One relic is the usual case and
    /// then this is just "the relic"; with two sculptures on two cables, taking
    /// the pixels of one and not the other is not a thing anyone means by
    /// "link cue".
    std::vector<RelicUsbOutput*> relicLinks(ShowState& show)
    {
        std::vector<RelicUsbOutput*> links;
        for (DeviceRuntime& device : show.devices)
        {
            if (!device.isLive())
            {
                continue;
            }
            if (RelicUsbOutput* relic = device.output->asRelicLink())
            {
                links.push_back(relic);
            }
        }
        return links;
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
            << " fixtures=" << show.config.fixtureCount()
            << " devices=" << show.devices.size()
            << " " << sharedBeatClock().describe(nowSeconds())
            << " " << sharedAudioLevel().describe(nowSeconds())
            << " vu=" << (sharedAudioLevel().isAnyLive(nowSeconds()) ? "live" : "none")
            << " midi=" << (show.midi.isOpen() ? ("\"" + show.midi.getPortName() + "\"") : "none")
            << " output=" << describeOutputs(show);
        return out.str();
    }

    /// Applies one line of the control protocol. Replies on stdout with OK or
    /// ERR so the wrapper can tell whether a command took.
    void handleCommand(ShowState& show, const std::string& rawLine)
    {
        // Strip a UTF-8 BOM. Anything that pipes a file of cues in - PowerShell
        // does it by default - puts one on the first line, and it turns a
        // perfectly good `link pixels` into `unknown command '<bom>link'`,
        // which is a genuinely baffling thing to read.
        std::string line = rawLine;
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF
                             && static_cast<unsigned char>(line[1]) == 0xBB
                             && static_cast<unsigned char>(line[2]) == 0xBF)
        {
            line.erase(0, 3);
        }

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

        if (command == "link")
        {
            // Every relic in the room, because "link cue" means "give the
            // sculptures their looks back", not "give one of them its look
            // back". With the usual single relic this reads the same.
            const std::vector<RelicUsbOutput*> relics = relicLinks(show);
            if (relics.empty())
            {
                emit("ERR link needs a device with a relic_usb output; this show has "
                   + describeOutputs(show));
                return;
            }

            if (words.size() < 2)
            {
                emit("ERR link needs pixels, cue, release, hello, cmd or bootsel");
                return;
            }

            std::string error;
            const std::string& what = words[1];

            if (what == "pixels" || what == "cue")
            {
                const bool cue = (what == "cue");
                for (RelicUsbOutput* relic : relics)
                {
                    relic->setMode(cue ? RelicUsbOutput::Mode::Cue : RelicUsbOutput::Mode::Pixels);

                    // Leaving pixel mode means handing the relic its own looks
                    // back now, rather than making it wait out the holdover
                    // with a frozen frame on it.
                    if (cue && !relic->release(error))
                    {
                        emit("WARN link release: " + error);
                    }
                }
                emit("OK link " + what);
                return;
            }

            if (what == "release")
            {
                for (RelicUsbOutput* relic : relics)
                {
                    if (!relic->release(error))
                    {
                        emit("ERR link " + error);
                        return;
                    }
                }
                emit("OK link release");
                return;
            }

            if (what == "bootsel")
            {
                for (RelicUsbOutput* relic : relics)
                {
                    if (!relic->rebootToBootloader(error))
                    {
                        emit("ERR link " + error);
                        return;
                    }
                }
                // The show keeps running and keeps rendering; there is simply
                // nothing on the other end of the cable any more. Stopping it
                // here would be a surprise when the point was to reflash.
                emit("OK link bootsel (the relic is in its bootloader now)");
                return;
            }

            if (what == "hello")
            {
                for (RelicUsbOutput* relic : relics)
                {
                    if (!relic->sendCommand("hello", error))
                    {
                        emit("ERR link " + error);
                        return;
                    }
                }
                emit("OK link hello");
                return;
            }

            if (what == "cmd")
            {
                if (words.size() < 3)
                {
                    emit("ERR link cmd needs something to send");
                    return;
                }

                // Everything after "link cmd", verbatim, because the relic's own
                // handleCommand parses whatever it likes and we should not be
                // in the middle of that.
                const size_t at = line.find("cmd");
                std::string text = line.substr(at + 3);
                while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                {
                    text.erase(text.begin());
                }
                while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
                {
                    text.pop_back();
                }

                bool sent = true;
                for (RelicUsbOutput* relic : relics)
                {
                    sent = sent && relic->sendCommand(text, error);
                }
                if (!sent)
                {
                    emit("ERR link " + error);
                    return;
                }
                emit("OK link cmd " + text);
                return;
            }

            emit("ERR link does not know '" + what + "'");
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
            emitParams(show);
            return;
        }

        if (command == "state")
        {
            if (words.size() < 2)
            {
                emit("ERR state needs a name");
                return;
            }

            // A cue reaches every end that renders. A relic in cue mode is
            // drawing its own pixels, so the line goes to it verbatim - blend
            // seconds included, its handleCommand decides whether it knows the
            // name, and this end has no business guessing at another device's
            // look list. And when our own pattern is a state machine it takes
            // the same cue: the scanner's ring and the obelisk on its cable
            // are one show changing looks together, not alternatives.
            const std::vector<RelicUsbOutput*> relics = relicLinks(show);
            bool anyCued = false;
            std::string error;
            for (RelicUsbOutput* relic : relics)
            {
                if (relic->getMode() != RelicUsbOutput::Mode::Cue)
                {
                    continue;
                }
                if (!relic->sendCommand(line, error))
                {
                    emit("ERR link " + error);
                    return;
                }
                anyCued = true;
            }

            StateMachinePattern* machine = show.pattern ? show.pattern->asStateMachine() : nullptr;
            if (!machine)
            {
                if (!anyCued)
                {
                    emit("ERR pattern '" + std::string(show.pattern ? show.pattern->getName() : "none")
                       + "' is not a state machine");
                    return;
                }
                show.lastCue = line;
                emit("OK state " + words[1] + " (to the relic)");
                return;
            }

            // `state <name> [seconds]` - the optional seconds are this blend's
            // length, which is how afterglow's transitionTo(state, time) pairs
            // arrive as one line. Sticky until the next override, like the
            // relic end of the same command.
            if (words.size() > 2)
            {
                machine->setTransitionTime(static_cast<float>(atof(words[2].c_str())));
            }

            if (!machine->setState(words[1], error))
            {
                // A name only the relic knows - `theater` cued at a scanner
                // show - still went to the relic above, and saying otherwise
                // would be a lie about the sculpture.
                emit("ERR " + error + (anyCued ? " (the cue relics took it)" : ""));
                return;
            }

            show.lastCue = line;
            emit("OK state " + words[1]);
            emit("STATE " + machine->currentStateName());
            emitParams(show);
            return;
        }

        if (command == "states")
        {
            emitStates(show);
            return;
        }

        if (command == "params")
        {
            // `params dump` prints the current set as one line of JSON, for
            // keeping a tuning session that was otherwise going to evaporate.
            // Nothing writes to a config file: what gets kept is a deliberate
            // paste, not a side effect of turning a knob.
            if (words.size() > 1 && words[1] == "dump")
            {
                if (!show.pattern)
                {
                    emit("DUMP {}");
                    emit("OK params dump 0");
                    return;
                }
                emit(dumpLine(*show.pattern));
                ecore::PropertyBag bag;
                show.pattern->reflect(bag);
                emit("OK params dump " + std::to_string(bag.size()));
                return;
            }

            emitParams(show);
            emit("OK params");
            return;
        }

        if (command == "param")
        {
            if (words.size() < 3)
            {
                emit("ERR param needs a name and a value");
                return;
            }

            if (!show.pattern)
            {
                emit("ERR no pattern");
                return;
            }

            // The echo of what it landed on goes out before the OK - see
            // applyParam - so a client that waits on the reply and then reads
            // has the new value and not the old one.
            if (!applyParam(*show.pattern, words[1], words[2], ""))
            {
                return;
            }

            emit("OK param " + words[1] + " " + words[2]);
            return;
        }

        if (command == "curve")
        {
            // `curve envelope 0:0 0.06:1:7 0.45:0` - the whole shape at once,
            // keys as t:v with an optional easing_functions index. Whole
            // rather than key-at-a-time on purpose: a shape is one edit, and
            // a client that could send half of one would sooner or later show
            // half of one.
            if (words.size() < 4)
            {
                emit("ERR curve needs a name and at least two t:v keys");
                return;
            }

            if (!show.pattern)
            {
                emit("ERR no pattern");
                return;
            }

            if (!applyCurve(*show.pattern, words[1], words, 2, ""))
            {
                return;
            }

            emit("OK curve " + words[1]);
            return;
        }

        if (command == "layers")
        {
            // Everything about every layer, for a client that attached late
            // or wants to be sure. The same lines it got at startup.
            for (ShowState::Layer& layer : show.layers)
            {
                announceLayer(layer);
            }
            emit("OK " + std::to_string(show.layers.size()) + " layers");
            return;
        }

        if (command == "layer")
        {
            // `layer <name> state <s> [seconds]` and, for the rest of what a
            // pattern takes - states, params [dump], param <k> <v>, curve <k>
            // keys... - the show's own commands with the layer's name in
            // front. Its announcements come back the same way: every line
            // the show's version emits, prefixed `LAYER <name>`. The reply
            // itself is a plain OK/ERR, because a reply is to a command and
            // a command is one at a time.
            if (words.size() < 3)
            {
                emit("ERR layer needs a name and a command (state, states, params, param, curve)");
                return;
            }

            ShowState::Layer* layer = findLayer(show, words[1]);
            if (layer == nullptr)
            {
                std::string known;
                for (const ShowState::Layer& each : show.layers)
                {
                    known += (known.empty() ? "" : ", ") + each.name;
                }
                emit("ERR no layer '" + words[1] + "'"
                   + (known.empty() ? " (this show has none)" : " (have: " + known + ")"));
                return;
            }

            const std::string prefix = "LAYER " + layer->name + " ";
            const std::string& sub = words[2];
            Pattern& pattern = *layer->pattern;

            if (sub == "state")
            {
                if (words.size() < 4)
                {
                    emit("ERR layer state needs a name");
                    return;
                }
                StateMachinePattern* machine = pattern.asStateMachine();
                if (machine == nullptr)
                {
                    emit("ERR layer '" + layer->name + "' runs '" + pattern.getName()
                       + "', which is not a state machine");
                    return;
                }
                if (words.size() > 4)
                {
                    machine->setTransitionTime(static_cast<float>(atof(words[4].c_str())));
                }
                std::string error;
                if (!machine->setState(words[3], error))
                {
                    emit("ERR layer " + layer->name + ": " + error);
                    return;
                }
                emit("OK layer " + layer->name + " state " + words[3]);
                emit(prefix + "STATE " + machine->currentStateName());
                emitParamsFor(&pattern, prefix);
                return;
            }

            if (sub == "states")
            {
                emitStatesFor(&pattern, prefix);
                emit("OK layer " + layer->name + " states");
                return;
            }

            if (sub == "params")
            {
                if (words.size() > 3 && words[3] == "dump")
                {
                    emit(prefix + dumpLine(pattern));
                    emit("OK layer " + layer->name + " params dump");
                    return;
                }
                emitParamsFor(&pattern, prefix);
                emit("OK layer " + layer->name + " params");
                return;
            }

            if (sub == "param")
            {
                if (words.size() < 5)
                {
                    emit("ERR layer param needs a name and a value");
                    return;
                }
                if (!applyParam(pattern, words[3], words[4], prefix))
                {
                    return;
                }
                emit("OK layer " + layer->name + " param " + words[3] + " " + words[4]);
                return;
            }

            if (sub == "curve")
            {
                if (words.size() < 6)
                {
                    emit("ERR layer curve needs a name and at least two t:v keys");
                    return;
                }
                if (!applyCurve(pattern, words[3], words, 4, prefix))
                {
                    return;
                }
                emit("OK layer " + layer->name + " curve " + words[3]);
                return;
            }

            emit("ERR layer: unknown command '" + sub + "' (state, states, params, param, curve)");
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
            // `beat div` is gone, and is rejected rather than ignored.
            //
            // It divided the beat count globally, which is the part that was
            // wrong - one look in half time is a decision, every look in half
            // time at once was a mode. What a look does with the beat is a knob
            // on the look now: `rate` for how often, attack and decay for the
            // shape. The rate it never had, once a bar, it has: the clock keeps
            // the bar, and `midi align` says where the one is.
            //
            // Said outright because the alternative is worse: without this the
            // word falls through to the tap below, and an old cue file asking
            // for `beat div 4` would silently shove the grid instead.
            if (words.size() >= 2 && (words[1] == "div" || words[1] == "divide"))
            {
                emit("ERR beat div is gone - it is a knob on the look now:"
                     " `param rate 0.25` for once a bar, 0.5 for half time, 2"
                     " for double time, and `param attack|decay <n>` for the"
                     " shape (`params` to see them). `midi align` says where"
                     " the one is");
                return;
            }

            // A beat, now: the tempo tapped in when there is no MIDI, and a
            // shove back into time when there is. Deliberately not the one -
            // `midi align` is that. Someone tapping a tempo in taps every beat,
            // and every tap declaring itself the top of the bar would leave the
            // slow rates hitting on all of them.
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
                   + sharedBeatClock().describe(nowSeconds()) + " "
                   + sharedAudioLevel().describe(nowSeconds()));
                emit("MIDI-OUT-STATUS " + show.midiOut.describe());
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
                const std::string spec = (words.size() >= 3) ? joinFrom(words, 2) : "auto";

                std::string midiError;
                if (!show.midi.open(spec, show.config.midi.ignore, &sharedBeatClock(), midiError))
                {
                    emit("ERR " + midiError);
                    return;
                }

                applyMidiConfig(show);

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

            if (action == "out")
            {
                // `midi out <spec>` opens, `midi out close` shuts, bare
                // `midi out` says where it is pointed.
                const std::string spec = (words.size() >= 3) ? joinFrom(words, 2) : "";

                if (spec.empty())
                {
                    emit("MIDI-OUT-STATUS " + show.midiOut.describe());
                    emit("OK midi out");
                    return;
                }

                if (spec == "close")
                {
                    show.midiOut.close();
                    emit("OK midi out close");
                    return;
                }

                std::string outError;
                if (!show.midiOut.open(spec, outError))
                {
                    emit("ERR " + outError);
                    return;
                }
                emit("OK midi out " + show.midiOut.getPortName());
                return;
            }

            if (action == "send")
            {
                // Bytes, in hex, one word each: `midi send F0 00 20 29 ... F7`.
                //
                // Hex rather than decimal because every controller's reference
                // manual is written in it, so a message can be typed straight
                // off the page and read back against it. Whitespace-separated
                // rather than one long string for the same reason - the manual
                // groups them that way.
                if (!show.midiOut.isOpen())
                {
                    emit("ERR midi send: no output open (midi out <spec>)");
                    return;
                }
                if (words.size() < 3)
                {
                    emit("ERR midi send: no bytes");
                    return;
                }

                std::vector<unsigned char> bytes;
                bytes.reserve(words.size() - 2);
                for (size_t index = 2; index < words.size(); ++index)
                {
                    const std::string& word = words[index];
                    if (word.empty() || word.size() > 2)
                    {
                        emit("ERR midi send: '" + word + "' is not a hex byte");
                        return;
                    }

                    unsigned int value = 0;
                    bool valid = true;
                    for (const char digit : word)
                    {
                        const int nibble = std::isxdigit(static_cast<unsigned char>(digit))
                            ? (std::isdigit(static_cast<unsigned char>(digit))
                                ? digit - '0'
                                : (std::tolower(static_cast<unsigned char>(digit)) - 'a' + 10))
                            : -1;
                        if (nibble < 0)
                        {
                            valid = false;
                            break;
                        }
                        value = (value << 4) | static_cast<unsigned int>(nibble);
                    }
                    if (!valid)
                    {
                        emit("ERR midi send: '" + word + "' is not a hex byte");
                        return;
                    }
                    bytes.push_back(static_cast<unsigned char>(value & 0xFF));
                }

                std::string outError;
                if (!show.midiOut.send(bytes.data(), bytes.size(), outError))
                {
                    emit("ERR " + outError);
                    return;
                }
                emit("OK midi send " + std::to_string(bytes.size()) + " bytes");
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

            emit("ERR midi: expected list, open, close, out, send, align, free-run, "
                 "monitor or status, got '" + action + "'");
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

    // --port is read ahead of the loop as well as in it, because the one-shot
    // actions below act the moment they are parsed and would otherwise not see
    // a --port that came after them on the line.
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::string(argv[i]) == "--port")
        {
            portOverride = argv[i + 1];
        }
    }

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
        else if (arg == "--link-selftest")
        {
            return runLinkSelfTest();
        }
        else if (arg == "--reboot-bootsel")
        {
            // Two ways in, tried in order of how much they can tell us.
            //
            // An elink Reboot frame is deliberate and confirmable: only a relic
            // running our firmware answers a Hello, so if one does we know
            // exactly what we are rebooting. The 1200-baud touch is the
            // fallback, and it is what every Arduino tool uses - it works on a
            // relic flashed before the link existed, which is precisely the
            // board you most want to reflash without the button.
            std::string target = portOverride;

            if (target.empty())
            {
                const std::vector<RelicProbe> found = probeRelicPorts(115200);
                if (!found.empty())
                {
                    target = found.front().port;
                    logLine("relic on " + target + ": " + found.front().identity);

                    RelicUsbOutput relic(target, 115200, RelicUsbOutput::Mode::Cue);
                    std::string error;
                    if (relic.open(error) && relic.rebootToBootloader(error))
                    {
                        emit("OK bootsel " + target);
                        return 0;
                    }
                    logLine("asking nicely failed (" + error + "); trying the 1200 touch");
                }
            }

            if (target.empty())
            {
                // Nothing answered, and the 1200-baud touch is not something to
                // aim at a guess. It is the fallback for a relic flashed before
                // the link existed - which we cannot tell apart from a DMX
                // widget, and rebooting a widget mid-show because it happened
                // to be the only port is not a mistake worth being capable of.
                //
                // So it has to be named. --list-ports says what is there.
                emit("ERR no relic answered a Hello. If this is a relic flashed"
                     " before the link existed, name its port: --reboot-bootsel"
                     " --port COMn (--list-ports shows them). Nothing was"
                     " touched.");
                return 1;
            }

            std::string error;
            if (!SerialPort::touchAt1200(target, error))
            {
                emit("ERR bootsel " + error);
                return 1;
            }

            emit("OK bootsel " + target + " (1200 touch; it is in the bootloader"
                                          " if the port has gone)");
            return 0;
        }
        else if (arg == "--probe-relics")
        {
            const std::vector<RelicProbe> found = probeRelicPorts(115200);
            for (const RelicProbe& relic : found)
            {
                emit("RELIC " + relic.port + "\t" + relic.identity);
            }
            emit("OK " + std::to_string(found.size()) + " relics");
            return found.empty() ? 1 : 0;
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
        size_t total = 0;
        for (const Device& device : show.config.devices)
        {
            // Report in each device's own numbering, so these line up with what
            // is dialled on the fixtures rather than with our internal 1-based
            // slots. Addressing is per device now: two rigs in one room need
            // not agree about it, and nothing says they will.
            const int bias = (device.addressing == Addressing::ZeroBased) ? -1 : 0;

            std::ostringstream header;
            header << "DEVICE " << device.name
                   << " fixtures=" << device.fixtures.size()
                   << " addressing=" << ((device.addressing == Addressing::ZeroBased) ? "zero" : "one")
                   << "-based"
                   << " coords=" << ((device.coordSpace == CoordSpace::Literal) ? "literal" : "normalized")
                   << " offset=" << device.placement.offsetX << "," << device.placement.offsetY
                   << " scale=" << device.placement.scaleX << "," << device.placement.scaleY
                   << " output=" << device.output.type;
            emit(header.str());

            int highest = 0;
            for (const Fixture& fixture : device.fixtures.all())
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

            emit("OK " + device.name + ": " + std::to_string(device.fixtures.size())
               + " fixtures, highest channel " + std::to_string(highest + bias));
            total += device.fixtures.size();
        }

        emit("OK " + std::to_string(total) + " fixtures over "
           + std::to_string(show.config.devices.size()) + " devices");
        return 0;
    }

    if (!patternOverride.empty()) show.config.pattern.name = patternOverride;
    if (!stateOverride.empty())   show.config.pattern.stateName = stateOverride;

    // Overrides that name a wire only make sense when there is one wire. The
    // rest apply to everything, which is what --dry-run is for.
    if (!portOverride.empty())
    {
        if (show.config.devices.size() != 1)
        {
            logLine("error: --port names one port, but this environment has "
                  + std::to_string(show.config.devices.size()) + " devices");
            emit("ERR --port is ambiguous with " + std::to_string(show.config.devices.size())
               + " devices; set it in the environment instead");
            return 1;
        }
        show.config.devices[0].output.port = portOverride;
    }

    for (Device& device : show.config.devices)
    {
        if (!deviceOverride.empty()) device.output.type = deviceOverride;
        if (fpsOverride > 0.0f)      device.output.fps = fpsOverride;
    }

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
    if (dryRun)
    {
        for (Device& device : show.config.devices)
        {
            device.output.type = "console";
        }
    }

    show.masterBrightness = show.config.master.brightness;

    // ---- giving a device its wire ----------------------------------------
    //
    // The part of bringing a device up that can fail for reasons that later go
    // away: a widget not plugged in yet, a device node udev has just recreated
    // with permissions nobody has fixed, a relic still booting. Startup calls
    // this once per device and the frame loop calls it again for anything still
    // offline, which is what lets a show that started with an empty USB port
    // light the moment the cable goes in — rather than needing a restart with
    // the room already full.
    //
    // Everything else about a device — its buffer, its strip, its place in the
    // fixture run — is built once and never torn down, because an offline
    // device still renders. Only the wire comes and goes.
    //
    // `announce` is off on a retry: the port it found and the rate it settled
    // on are worth saying the first time and are noise every two seconds after.
    const auto attachWire = [&](DeviceRuntime& device, bool announce) -> bool
    {
        const Device& config = *device.config;
        const std::string where = "[" + config.name + "] ";
        std::string error;

        device.offline.clear();

        // ---- resolve its port --------------------------------------------
        std::string port = config.output.port;
        if (port == "auto")
        {
            const bool isRelic = config.output.type == "relic_usb"
                              || config.output.type == "relic_usb_cue";

            // A relic is found by asking, not by guessing at a port name: on
            // this machine the DMX widget and a Pico are both "COMn" with a
            // description that names neither, and picking the widget would mean
            // a show driving something that ignores it. See probeRelicPorts.
            port = isRelic ? autoDetectRelicPort(config.output.baud) : autoDetectPort();

            const bool needsWire = config.output.type != "console"
                                && config.output.type != "preview"
                                && config.output.type != "none"
                                && config.output.type != "null";
            // An Open DMX widget does not need a tty node to be reachable, and
            // frequently does not have one: libftdi takes the chip off
            // ftdi_sio to talk to it, which removes /dev/ttyUSBn for as long as
            // it holds it. Leaving the port empty lets the output find the
            // widget by USB instead. See edmx/ftdi_dmx.h.
            const bool findsItsOwnWidget = (config.output.type == "enttec_open")
                                        && ftdiCanSeeAWidget();

            if (port.empty() && isRelic)
            {
                device.offline = "no relic answered on any port";
            }
            else if (port.empty() && needsWire && !findsItsOwnWidget)
            {
                device.offline = "no serial port found";
            }
            else if (!port.empty() && announce)
            {
                logLine(where + "auto-detected port " + port);
            }
        }

        if (device.offline.empty())
        {
            device.output = makeDmxOutput(config.output.type, port, config.output.baud,
                                          config.output.consoleChannels, error);
            if (!device.output)
            {
                device.offline = error;
            }
        }

        if (device.output)
        {
            device.output->setUniverseLength(config.fixtures.highestChannel());
            if (!device.output->open(error))
            {
                device.output.reset();
                device.offline = error;
            }
        }

        if (!device.offline.empty())
        {
            return false;
        }

        // ---- do not outrun the wire --------------------------------------
        // Asking for more frames than the link can carry does not make the rig
        // faster. The driver queues the excess, the backlog grows without
        // bound, and the widget ends up parsing half-written frames - which
        // reads as a strobing, unblendable rig rather than as an error. Send at
        // a rate the wire can deliver and every frame lands whole.
        device.fps = config.output.fps;

        const float outputCeiling = device.output->maxFrameRate();
        if (outputCeiling > 0.0f && device.fps > outputCeiling)
        {
            if (announce)
            {
                std::ostringstream note;
                note.setf(std::ios::fixed);
                note.precision(1);
                note << config.name << ": fps " << device.fps
                     << " is more than " << device.output->describe()
                     << " can carry; running at " << outputCeiling
                     << ". Raise its baud for a faster refresh.";
                logLine("warning: " + note.str());
                emit("WARN " + note.str());
            }

            device.fps = outputCeiling;
        }

        return true;
    };

    // ---- bring every device up -------------------------------------------
    show.devices.resize(show.config.devices.size());

    for (size_t i = 0; i < show.config.devices.size(); ++i)
    {
        Device& config = show.config.devices[i];
        DeviceRuntime& device = show.devices[i];

        device.config = &config;
        device.firstFixture = show.config.firstFixtureOf(i);
        device.fixtureCount = config.fixtures.size();

        const std::string where = "[" + config.name + "] ";

        // ---- send only the channels this device actually uses ------------
        // A receiver keeps whatever it already had for slots that do not
        // arrive, so there is nothing to gain from shipping 442 trailing zeros
        // every frame. On a serial link that padding is most of the frame time.
        //
        // The same number sizes the frame buffer, which on a pixel rig is
        // longer than a universe: the obelisk's 344 nodes are 1032 channels.
        //
        // Sized whether or not there is a wire: an offline device still renders
        // into its buffer, which is what keeps it in the frame stream and on
        // the viewer's screen.
        device.universe.resize(config.fixtures.highestChannel());

        attachWire(device, /*announce=*/true);

        // One HSV node per fixture. This is the same framebuffer the
        // microcontroller build hands to the LEDs; here it feeds the patch.
        // Built for every device, wired or not, for the same reason the buffer
        // above is sized: an offline device still renders.
        device.strip.reset(new eio::HSVStrip(static_cast<uint16_t>(device.fixtureCount), 0));

        if (!device.offline.empty())
        {
            // An error, and not a fatal one. It is an error because a device
            // silently missing from a show is how you find out at the venue;
            // it is not fatal because the whole rig refusing to start over one
            // absent cable is the same failure the MIDI open below declines to
            // make - and because building a look with nothing plugged in is a
            // thing this program is *for*.
            //
            // OFFLINE rather than ERR: on this protocol ERR is how a command is
            // refused, and a UI reading one as the other either waits forever
            // for a reply or gives up on a show that is running fine.
            logLine("error: " + where + device.offline + "; this device is offline");
            emit("OFFLINE " + config.name + ": " + device.offline);

            // It will be asked again, by the frame loop. Not a promise that it
            // comes back - only that nobody has to restart the show to find out.
            device.reportedOffline = device.offline;
            continue;
        }

        logLine(where + "output: " + device.output->describe());
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
        applyMidiConfig(show);

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

    // ---- the shape of the whole show -------------------------------------
    const size_t fixtureCount = show.config.fixtureCount();
    show.context.fixtureCount = fixtureCount;

    // 0..1 across every device in order, for the patterns that sweep along a
    // rig without caring about its shape. With two devices a chase runs through
    // the first and on into the second, which is what you would want it to do.
    show.context.positions.resize(fixtureCount);
    for (size_t idx = 0; idx < fixtureCount; ++idx)
    {
        show.context.positions[idx] = (fixtureCount <= 1)
            ? 0.0f
            : static_cast<float>(idx) / static_cast<float>(fixtureCount - 1);
    }

    // Fills nodeCoords, which is where the placements are actually applied.
    applyCoordFrame(show);

    // The layers, once the nodes they slice exist. A name that resolves to
    // nothing is fatal here, like a missing device file: it is a typo.
    if (!buildLayers(show, error))
    {
        logLine("config error: " + error);
        emit("ERR config " + error);
        return 1;
    }

    {
        std::ostringstream ready;
        ready << "READY fixtures=" << fixtureCount
              << " devices=" << show.devices.size()
              << " pattern=" << show.config.pattern.name;
        emit(ready.str());
    }

    // Which fixtures belong to which device, so a UI can draw them apart. Sent
    // before the first frame, like STATES, so the window is built by the time
    // colour arrives.
    for (size_t i = 0; i < show.devices.size(); ++i)
    {
        const DeviceRuntime& device = show.devices[i];
        std::ostringstream line;
        line << "DEVICE " << i
             << " " << device.name()
             << " " << device.firstFixture
             << " " << device.fixtureCount
             << " " << device.describeOutput();
        emit(line.str());
    }

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
        // Name the fixtures once, so the frame lines can stay compact. Across
        // every device, in the order the frame reports them; the DEVICE lines
        // above say where each one's run starts.
        std::ostringstream names;
        names << "FIXTURES";
        for (const Device& device : show.config.devices)
        {
            for (const Fixture& fixture : device.fixtures.all())
            {
                names << " " << fixture.name;
            }
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

    // The loop renders at whatever the *fastest* device wants, and each device
    // sends on its own schedule below. Rendering at the slowest would hold a
    // 40fps truss back to a relic's 30; rendering at the fastest and letting
    // the relic skip costs one extra render and keeps both correct.
    float showFps = 1.0f;
    for (const DeviceRuntime& device : show.devices)
    {
        showFps = std::max(showFps, device.fps);
    }
    const auto framePeriod = std::chrono::duration<double>(1.0 / static_cast<double>(showFps));

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

        // Before anything ticks: which of this frame's beats are hits is one
        // question with one answer, and every look that fires on the beat is
        // about to read it. Working it out per look is what used to let the UV
        // and the truss fall out of step - see edmx/beat_trigger.h.
        sharedTriggerRack().tick(nowSeconds());

        show.pattern->tick(deltaTime);
        show.pattern->render(show.context, show.colors);

        // The layers, over the show: each renders its own few fixtures as a
        // rig of their own and writes the result over what the show put
        // there. The UV par flashing on the beat while the truss around it
        // follows the scanner is one layer of one fixture.
        for (ShowState::Layer& layer : show.layers)
        {
            layer.pattern->tick(deltaTime);
            layer.pattern->render(layer.context, layer.colors);
            for (size_t idx = 0; idx < layer.fixtures.size() && idx < layer.colors.size(); ++idx)
            {
                const size_t at = layer.fixtures[idx];
                if (at < show.colors.size())
                {
                    show.colors[at] = layer.colors[idx];
                }
            }
        }

        // Once, after the first frame. A state machine's looks are not built
        // until it has been rendered — that is when it learns the rig's shape —
        // so announcing the knobs any earlier announces an empty set. The
        // layers likewise, and after the show's so a client that reads
        // blocks in order finishes the show's before a layer's begins.
        if (framesRendered == 0)
        {
            emitParams(show);
            for (ShowState::Layer& layer : show.layers)
            {
                announceLayer(layer);
            }
        }

        // One render, then each device takes its own slice of it. This is the
        // point of the whole arrangement: the look was computed once, in one
        // coordinate space, and the sculpture and the truss are looking at
        // different parts of the same picture rather than running it twice.
        const float master = show.blackout ? 0.0f : show.masterBrightness;
        const double frameSeconds = std::chrono::duration<double>(now.time_since_epoch()).count();

        // ---- anything still without a wire -------------------------------
        // Ask again, every kDeviceRetrySeconds. This is the widget that was not
        // plugged in when the show started, the one that got knocked out of the
        // USB port mid-set, and the device node that came back owned by a group
        // this user is not in until somebody fixes it - all of which are a rig
        // that should light when the problem goes away rather than one that
        // sits dark because of how things were at startup.
        for (DeviceRuntime& device : show.devices)
        {
            if (device.isLive())
            {
                continue;
            }

            if (device.nextRetryAt <= 0.0)
            {
                device.nextRetryAt = frameSeconds + kDeviceRetrySeconds;
                continue;
            }
            if (frameSeconds < device.nextRetryAt)
            {
                continue;
            }
            device.nextRetryAt = frameSeconds + kDeviceRetrySeconds;

            if (attachWire(device, /*announce=*/false))
            {
                logLine("[" + device.name() + "] output: " + device.output->describe());
                emit("ONLINE " + device.name() + ": " + device.output->describe());
                device.reportedOffline.clear();

                // A relic that arrives mid-show missed every cue before now.
                // In cue mode the current look is the entire contract, so
                // replay the last one - the sculpture joins the show where it
                // is, not at whatever its firmware idles in. Best-effort: if
                // this write fails the wire is already on its way back to the
                // retry path above.
                RelicUsbOutput* relic = device.output->asRelicLink();
                if (relic && relic->getMode() == RelicUsbOutput::Mode::Cue && !show.lastCue.empty())
                {
                    std::string cueError;
                    relic->sendCommand(show.lastCue, cueError);
                }
            }
            else if (device.offline != device.reportedOffline)
            {
                // Only when the answer changes. A widget that is simply not
                // there yet would otherwise print the same line all night, and
                // a log that repeats itself is one nobody reads.
                device.reportedOffline = device.offline;
                logLine("[" + device.name() + "] still offline: " + device.offline);
                emit("OFFLINE " + device.name() + ": " + device.offline);
            }
        }

        bool outputFailed = false;
        for (DeviceRuntime& device : show.devices)
        {
            // Push through the strip so the desktop path and the
            // microcontroller path agree on what a frame is.
            for (size_t idx = 0; idx < device.fixtureCount; ++idx)
            {
                const size_t at = device.firstFixture + idx;
                if (at < show.colors.size())
                {
                    device.strip->setHSV(static_cast<uint16_t>(idx), show.colors[at]);
                }
            }

            device.universe.clear();
            device.config->fixtures.render(device.strip->getStripHSV(),
                                           master * device.config->brightness,
                                           show.config.master.gamma,
                                           device.universe);

            // Rendered above, sent here - and only the sending needs a wire.
            // An offline device has done its work by now: its colours are in
            // the frame stream and on the viewer's screen.
            if (!device.isLive())
            {
                continue;
            }

            // Its own rate, not the loop's. A relic that draws at 30 simply
            // does not get every frame a 40fps truss does.
            if (frameSeconds < device.nextSendAt)
            {
                continue;
            }
            device.nextSendAt = std::max(frameSeconds, device.nextSendAt)
                              + 1.0 / static_cast<double>(std::max(device.fps, 1.0f));

            if (!device.output->sendFrame(device.universe, error))
            {
                logLine("output error: [" + device.name() + "] " + error);
                emit("ERR output " + device.name() + ": " + error);
                outputFailed = true;
                break;
            }
        }

        if (outputFailed)
        {
            exitCode = 1;
            break;
        }

        // What the relic has to say. It answers a Hello by naming its strips and
        // their lengths, and it says so when a takeover starts or lapses - all
        // of which is worth having in the log of a show rather than only in a
        // serial monitor nobody has open.
        for (DeviceRuntime& device : show.devices)
        {
            RelicUsbOutput* relic = device.isLive() ? device.output->asRelicLink() : nullptr;
            if (!relic)
            {
                continue;
            }
            for (const std::string& said : relic->drainRelicLines())
            {
                // Named, because two sculptures on two cables both talk.
                emit("RELIC " + device.name() + " " + said);
            }
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
                // Every device's fixtures, in device order - the same order the
                // DEVICE and FIXTURES lines described, so a viewer can slice
                // this back apart without being told again every frame.
                for (const DeviceRuntime& device : show.devices)
                {
                    for (const Fixture& fixture : device.config->fixtures.all())
                    {
                        char swatch[8];
                        std::snprintf(swatch, sizeof(swatch), " %02x%02x%02x",
                                      device.universe.getChannel(fixture.startChannel + fixture.offsetR),
                                      device.universe.getChannel(fixture.startChannel + fixture.offsetG),
                                      device.universe.getChannel(fixture.startChannel + fixture.offsetB));
                        frame << swatch;
                    }
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
    std::string shutdownError;
    for (DeviceRuntime& device : show.devices)
    {
        if (!device.isLive())
        {
            continue;
        }
        device.universe.clear();
        device.config->fixtures.render(std::vector<ecore::HSV>(device.fixtureCount), 0.0f,
                                       show.config.master.gamma, device.universe);
        device.output->sendFrame(device.universe, shutdownError);
        device.output->close();
    }

    // Before the reader goes, so the MIDI callback cannot fire into a clock
    // whose owner is on its way out.
    show.midi.close();

    const bool stdinStuck = stdinReader.stop();

    emit("DONE frames=" + std::to_string(framesRendered));

    if (stdinStuck)
    {
        // Detaching the reader was not the whole story. On glibc the thread
        // is blocked in read() *holding stdin's stream lock*, and exit()'s
        // stdio cleanup takes every stream's lock to flush it - so a --frames
        // run at an interactive terminal printed DONE and then hung on a lock
        // that would only be released by a keypress. Everything real is
        // already shut down by hand above (dark frame sent, ports closed,
        // MIDI closed, cout flushed by emit), so skip the teardown that
        // deadlocks rather than perform it.
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(exitCode);
    }

    return exitCode;
}
