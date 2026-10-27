// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <enet/host_udp.h>
#include <eosc/binding.h>
#include <eosc/message.h>
#include <eosc/transport.h>
#include <ebeat/level_bus.h>


///
/// OSC in and out, for the desk. The protocol is the library's (eosc), the
/// socket is the library's (enet::HostUdp); this is the desk's use of them:
/// a map of bindings run on the input thread, with the audio bus filled in
/// process and everything else reported as a line for the python side to act
/// on, and an output that streams one fixture's colour to a visualiser and
/// sends whatever else it is asked to.
///

namespace edesk
{
    /// One address as seen on the wire, for `osc seen`.
    struct OscSeen
    {
        std::string address;
        unsigned long long count{0};
        float low{0.0f};
        float high{0.0f};
        bool numeric{false};
        std::string lastArgs;   ///< JSON array of the last arguments
        double lastAt{-1.0};
    };

    /// A binding as the map file names it: the library binding, plus the
    /// action the python side runs when it fires. `audio` is run here.
    struct OscMapEntry
    {
        eosc::Binding binding;
        std::string action;
        std::map<std::string, std::string> params;
        /// `audio` with a channel the bus has: filled in process, never a line.
        bool native{false};
        ebeat::AudioChannel channel{ebeat::AudioChannel::Level};
        float low{0.0f};
        float high{1.0f};
    };

    class OscInput
    {
    public:
        OscInput() = default;
        ~OscInput();
        OscInput(const OscInput&) = delete;
        OscInput& operator=(const OscInput&) = delete;

        /// Opens the port and starts the reader. Safe on an open input.
        bool listen(const std::string& host, int port, std::string& outError);
        void close();
        bool isListening() const { return running.load(); }
        int getPort() const { return port; }
        std::string getHost() const { return host; }

        /// Loads a map file (the python side's BindingSet format). Replaces
        /// the current map. `outNative` says how many are run in process.
        bool loadMap(const std::string& path, std::string& outError, int& outLoaded, int& outNative);
        void clearMap();
        std::vector<OscMapEntry> entries() const;

        /// Report every message as an `OSC-IN` line. Off by default.
        void setWatch(bool enable) { watching.store(enable); }
        bool isWatching() const { return watching.load(); }

        /// Globs whose messages are always reported, watch or no watch.
        void addAnnounce(const std::string& glob);
        void clearAnnounce();
        std::vector<std::string> announces() const;

        /// Lines for the protocol, oldest first: `OSC-IN`, `OSC-FIRE`, and a
        /// drop count. Called from the frame loop.
        std::vector<std::string> drain();

        /// Every address seen since the port opened, or since forget().
        std::vector<OscSeen> seen() const;
        void forget();

        unsigned long long getPackets() const { return oscPort.getPackets(); }
        unsigned long long getMessages() const { return messages.load(); }
        unsigned long long getUndecodable() const { return oscPort.getUndecodable(); }
        unsigned long long getFires() const { return fires.load(); }
        double getLastHeardAt() const { return lastHeardAt.load(); }
        std::string getMapPath() const;

        /// `port=7000 map=synesthesia.json bindings=20 native=19 packets=4821 messages=19284 heard=0.1s`
        std::string describe(double now) const;

        /// One decoded message, on the reader thread. Public for the self-test.
        void handleMessage(const eosc::Message& message, double when);

    private:
        void read();
        void push(const std::string& line);

        static constexpr size_t kMaxLines = 512;
        static constexpr size_t kMaxSeen = 512;

        enet::HostUdp socket;
        eosc::OscPort oscPort;
        std::thread reader;
        std::atomic<bool> running{false};
        std::string host;
        int port{0};

        mutable std::mutex lock;
        std::vector<OscMapEntry> map;
        std::string mapPath;
        std::vector<std::string> announce;
        std::vector<std::string> lines;
        unsigned long long dropped{0};
        std::map<std::string, OscSeen> seenByAddress;
        unsigned long long seenOverflow{0};

        std::atomic<bool> watching{false};
        std::atomic<unsigned long long> messages{0};
        std::atomic<unsigned long long> fires{0};
        std::atomic<double> lastHeardAt{-1.0};
    };

    class OscOutput
    {
    public:
        OscOutput() = default;
        ~OscOutput();
        OscOutput(const OscOutput&) = delete;
        OscOutput& operator=(const OscOutput&) = delete;

        /// Points at `host:port`. A name is looked up now and again every
        /// `resolveEvery` seconds on its own thread, and the socket re-pointed
        /// when the answer moves; a name that does not answer yet is not an
        /// error, just nowhere to send until it does.
        bool open(const std::string& endpoint, std::string& outError, double resolveEvery = 15.0);
        void close();
        bool isOpen() const { return opened.load(); }

        /// The colour control, and how a colour is sent to it.
        void setControl(const std::string& address) { std::lock_guard<std::mutex> guard(lock); control = address; }
        void setSeparate(bool enable) { separate.store(enable); }
        void setGamma(float inGamma) { gamma.store(inGamma); }
        std::string getControl() const { std::lock_guard<std::mutex> guard(lock); return control; }
        bool getSeparate() const { return separate.load(); }
        float getGamma() const { return gamma.load(); }

        /// The fixture whose colour is streamed every frame, or -1 for none.
        void setTapFixture(int index) { tapFixture.store(index); }
        int getTapFixture() const { return tapFixture.load(); }

        bool send(const eosc::Message& message);
        /// One 0..255 colour to the control, as normalised floats, ungamma'd.
        bool sendColor(int r, int g, int b);

        std::vector<std::string> drain();
        std::vector<OscSeen> sent() const;
        void forget();

        unsigned long long getSent() const { return sentCount.load(); }
        unsigned long long getDropped() const { return droppedCount.load(); }
        std::string getLastError() const { std::lock_guard<std::mutex> guard(lock); return lastError; }
        std::string getEndpoint() const { std::lock_guard<std::mutex> guard(lock); return endpoint; }
        std::string getAddress() const { std::lock_guard<std::mutex> guard(lock); return resolved; }

        /// `mac-mini.local:6000 -> 192.168.1.20:6000 /controls/global/color/1 (one message, 3 floats) sent=42 dropped=0`
        std::string describe() const;

    private:
        bool pointAt(std::string& outError);
        void watch();
        void record(const eosc::Message& message, bool delivered);

        enet::HostUdp socket;
        eosc::OscPort oscPort;
        std::thread resolver;
        std::atomic<bool> opened{false};
        std::atomic<bool> stopping{false};
        std::atomic<bool> separate{false};
        std::atomic<float> gamma{0.0f};
        std::atomic<int> tapFixture{-1};
        std::atomic<unsigned long long> sentCount{0};
        std::atomic<unsigned long long> droppedCount{0};

        mutable std::mutex lock;
        std::string endpoint;
        std::string host;
        int port{0};
        bool isName{false};
        double resolveEvery{15.0};
        std::string resolved;
        std::string control{"/controls/global/color/1"};
        std::string lastError;
        std::vector<std::string> lines;
        std::map<std::string, OscSeen> sentByAddress;
    };

    /// Arguments as a JSON array, the way `OSC-IN` and `OSC-SEEN` print them.
    std::string oscArgsJson(const eosc::Message& message);

    /// `host:port` or `host` with a default port. False with the reason.
    bool parseOscEndpoint(const std::string& text, int defaultPort, std::string& outHost, int& outPort,
                          std::string& outError);

    /// Words after an address into typed arguments: `1.5` a float, `3` an
    /// int, `true`/`false` a bool, anything else a string.
    void oscArgsFromWords(const std::vector<std::string>& words, size_t first, eosc::Message& message);

    /// The same from one line of text, where `"a name"` is one string with
    /// spaces in it (`\"` and `\\` inside it).
    void oscArgsFromText(const std::string& text, eosc::Message& message);
}
