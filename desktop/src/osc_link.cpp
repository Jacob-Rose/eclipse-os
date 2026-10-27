// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edesk/osc_link.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include <eosc/address.h>
#include <ebeat/shared.h>

#include "edesk/json.h"

using namespace edesk;
using namespace ebeat;

// ---- text helpers --------------------------------------------------------------

namespace
{
    std::string jsonString(const std::string& text)
    {
        std::string out("\"");
        for (char c : text)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char code[8];
                        std::snprintf(code, sizeof(code), "\\u%04x", static_cast<unsigned>(c));
                        out += code;
                    }
                    else
                    {
                        out.push_back(c);
                    }
            }
        }
        out.push_back('"');
        return out;
    }

    std::string number(double value)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.6g", value);
        return std::string(text);
    }

    bool looksLikeInt(const std::string& word)
    {
        if (word.empty()) return false;
        size_t i = (word[0] == '-' || word[0] == '+') ? 1 : 0;
        if (i >= word.size()) return false;
        for (; i < word.size(); ++i)
        {
            if (word[i] < '0' || word[i] > '9') return false;
        }
        return true;
    }

    bool looksLikeFloat(const std::string& word, float& outValue)
    {
        if (word.empty()) return false;
        char* end = nullptr;
        const double value = std::strtod(word.c_str(), &end);
        if (end == word.c_str() || *end != '\0') return false;
        outValue = static_cast<float>(value);
        return true;
    }

    void recordSeen(std::map<std::string, OscSeen>& table, unsigned long long& overflow, size_t cap,
                    const eosc::Message& message, double when)
    {
        auto found = table.find(message.address);
        if (found == table.end())
        {
            if (table.size() >= cap)
            {
                ++overflow;
                return;
            }
            found = table.emplace(message.address, OscSeen{}).first;
            found->second.address = message.address;
        }
        OscSeen& seen = found->second;
        ++seen.count;
        seen.lastArgs = oscArgsJson(message);
        seen.lastAt = when;
        float value = 0.0f;
        if (message.number(0, value))
        {
            if (!seen.numeric)
            {
                seen.numeric = true;
                seen.low = value;
                seen.high = value;
            }
            seen.low = std::min(seen.low, value);
            seen.high = std::max(seen.high, value);
        }
    }

    std::vector<OscSeen> snapshot(const std::map<std::string, OscSeen>& table)
    {
        std::vector<OscSeen> out;
        out.reserve(table.size());
        for (const auto& entry : table) out.push_back(entry.second);
        return out;
    }
}

std::string edesk::oscArgsJson(const eosc::Message& message)
{
    std::string out("[");
    for (size_t i = 0; i < message.args.size(); ++i)
    {
        const eosc::Arg& arg = message.args[i];
        if (i > 0) out += ", ";
        switch (arg.type)
        {
            case eosc::Arg::Type::Int32:   out += std::to_string(arg.i); break;
            case eosc::Arg::Type::Float32: out += number(arg.f); break;
            case eosc::Arg::Type::Float64: out += number(arg.d); break;
            case eosc::Arg::Type::String:  out += jsonString(arg.bytes); break;
            case eosc::Arg::Type::Blob:    out += jsonString("<blob " + std::to_string(arg.bytes.size()) + " bytes>"); break;
            case eosc::Arg::Type::True:    out += "true"; break;
            case eosc::Arg::Type::False:   out += "false"; break;
            default:                       out += "null"; break;
        }
    }
    out.push_back(']');
    return out;
}

bool edesk::parseOscEndpoint(const std::string& text, int defaultPort, std::string& outHost, int& outPort,
                             std::string& outError)
{
    std::string trimmed = text;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
    if (trimmed.empty())
    {
        outError = "an endpoint is host or host:port";
        return false;
    }
    outPort = defaultPort;
    std::string host = trimmed;
    if (!trimmed.empty() && trimmed[0] == '[')
    {
        // [v6]:port
        const size_t close = trimmed.find(']');
        if (close == std::string::npos)
        {
            outError = "'" + text + "': no closing bracket";
            return false;
        }
        host = trimmed.substr(1, close - 1);
        if (close + 1 < trimmed.size() && trimmed[close + 1] == ':')
        {
            trimmed = host + ":" + trimmed.substr(close + 2);
        }
        else
        {
            trimmed = host;
        }
    }
    const size_t colon = trimmed.rfind(':');
    if (colon != std::string::npos && trimmed.find(':') == colon)
    {
        host = trimmed.substr(0, colon);
        const std::string portText = trimmed.substr(colon + 1);
        if (!looksLikeInt(portText))
        {
            outError = "'" + text + "': the port is not a number";
            return false;
        }
        outPort = std::stoi(portText);
    }
    if (host.empty() || outPort < 0 || outPort > 65535)
    {
        outError = "'" + text + "': not a host and a port 0..65535";
        return false;
    }
    outHost = host;
    return true;
}

void edesk::oscArgsFromWords(const std::vector<std::string>& words, size_t first, eosc::Message& message)
{
    for (size_t i = first; i < words.size(); ++i)
    {
        const std::string& word = words[i];
        float asFloat = 0.0f;
        if (word == "true")       message.add(true);
        else if (word == "false") message.add(false);
        else if (looksLikeInt(word)) message.add(static_cast<int32_t>(std::stol(word)));
        else if (looksLikeFloat(word, asFloat)) message.add(asFloat);
        else message.add(word);
    }
}

void edesk::oscArgsFromText(const std::string& text, eosc::Message& message)
{
    std::vector<std::string> words;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i >= text.size()) break;
        if (text[i] == '"')
        {
            std::string word;
            ++i;
            while (i < text.size() && text[i] != '"')
            {
                if (text[i] == '\\' && i + 1 < text.size()) ++i;
                word.push_back(text[i]);
                ++i;
            }
            ++i;   // the closing quote, if there was one
            message.add(word);   // quoted: a string whatever it looks like
            continue;
        }
        const size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t') ++i;
        words.push_back(text.substr(start, i - start));
        oscArgsFromWords(words, words.size() - 1, message);
    }
}

// ---- input ---------------------------------------------------------------------

namespace
{
    struct IntoInput : public eosc::MessageSink
    {
        OscInput& input;
        explicit IntoInput(OscInput& inInput) : input(inInput) {}
        void onMessage(const eosc::Message& message, double when) override { input.handleMessage(message, when); }
    };
}

OscInput::OscInput() : busLevels(sharedAudioLevel())
{
    busBag.reflect("audio", busLevels);
}

OscInput::~OscInput()
{
    close();
}

bool OscInput::listen(const std::string& inHost, int inPort, std::string& outError)
{
    close();
    if (!socket.listen(inHost, inPort, outError))
    {
        return false;
    }
    socket.setTimeout(250);
    oscPort.setTransport(&socket);
    host = inHost;
    port = socket.getLocalPort();
    running.store(true);
    reader = std::thread([this]() { read(); });
    return true;
}

void OscInput::close()
{
    if (running.exchange(false))
    {
        if (reader.joinable()) reader.join();
    }
    socket.close();
    oscPort.setTransport(nullptr);
    port = 0;
}

void OscInput::read()
{
    IntoInput sink(*this);
    while (running.load())
    {
        // the socket parks up to its timeout, so this loop idles cheaply
        oscPort.tick(nowSeconds(), sink);
    }
}

void OscInput::push(const std::string& line)
{
    if (lines.size() >= kMaxLines)
    {
        ++dropped;
        return;
    }
    lines.push_back(line);
}

void OscInput::handleMessage(const eosc::Message& message, double when)
{
    messages.fetch_add(1);
    lastHeardAt.store(when);

    std::lock_guard<std::mutex> guard(lock);
    recordSeen(seenByAddress, seenOverflow, kMaxSeen, message, when);

    bool report = watching.load();
    for (const std::string& glob : announce)
    {
        if (report) break;
        report = eosc::matchAddress(glob, message.address);
    }
    if (report)
    {
        push("OSC-IN " + message.address + " " + oscArgsJson(message));
    }

    busLevels.when = when;
    const int landed = toBus.apply(message, when, busBag);
    const int fired = reported.handle(message, when, [this](const eosc::Binding& binding, float value)
    {
        push("OSC-FIRE " + jsonString(binding.label) + " " + number(value));
    });
    fires.fetch_add(static_cast<unsigned long long>(landed + fired));

    // the knobs belong to the frame loop: what one of them wants waits for it
    for (const eosc::Binding& binding : toLooks.all())
    {
        if (binding.enabled && binding.matches(message.address))
        {
            if (arrived.size() < kMaxArrived) arrived.push_back({message, when});
            else ++dropped;
            break;
        }
    }
}

bool OscInput::hasArrived() const
{
    std::lock_guard<std::mutex> guard(lock);
    return !arrived.empty();
}

int OscInput::land(ecore::PropertyBag& looks)
{
    std::lock_guard<std::mutex> guard(lock);
    int landed = 0;
    for (const Arrived& one : arrived)
    {
        landed += toLooks.apply(one.message, one.when, looks);
    }
    arrived.clear();
    fires.fetch_add(static_cast<unsigned long long>(landed));
    return landed;
}

std::vector<std::string> OscInput::drain()
{
    std::lock_guard<std::mutex> guard(lock);
    std::vector<std::string> out;
    out.swap(lines);
    if (dropped > 0)
    {
        out.push_back("OSC-IN dropped " + std::to_string(dropped));
        dropped = 0;
    }
    return out;
}

std::vector<OscSeen> OscInput::seen() const
{
    std::lock_guard<std::mutex> guard(lock);
    return snapshot(seenByAddress);
}

void OscInput::forget()
{
    std::lock_guard<std::mutex> guard(lock);
    seenByAddress.clear();
    seenOverflow = 0;
}

void OscInput::addAnnounce(const std::string& glob)
{
    std::lock_guard<std::mutex> guard(lock);
    if (std::find(announce.begin(), announce.end(), glob) == announce.end())
    {
        announce.push_back(glob);
    }
}

void OscInput::clearAnnounce()
{
    std::lock_guard<std::mutex> guard(lock);
    announce.clear();
}

std::vector<std::string> OscInput::announces() const
{
    std::lock_guard<std::mutex> guard(lock);
    return announce;
}

std::string OscInput::getMapPath() const
{
    std::lock_guard<std::mutex> guard(lock);
    return mapPath;
}

void OscInput::clearMap()
{
    std::lock_guard<std::mutex> guard(lock);
    toBus.clear();
    toLooks.clear();
    reported.clear();
    arrived.clear();
    mapPath.clear();
}

bool OscInput::loadMap(const std::string& path, std::string& outError, int& outLoaded, int& outNative)
{
    outLoaded = 0;
    outNative = 0;
    JsonValue root;
    if (!parseJsonFile(path, root, outError))
    {
        return false;
    }
    const JsonValue& list = root["bindings"];
    if (!list.isArray())
    {
        outError = path + ": no \"bindings\" array";
        return false;
    }

    eosc::BindingSet bus;
    eosc::BindingSet looks;
    eosc::BindingSet rest;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const JsonValue& item = list[i];
        eosc::Binding binding;
        binding.label = item["label"].asString("binding");
        const JsonValue& trigger = item["trigger"];
        binding.pattern = trigger["pattern"].asString("*");
        const JsonValue& exclude = trigger["exclude"];
        if (exclude.isArray())
        {
            for (size_t e = 0; e < exclude.size(); ++e) binding.exclude.push_back(exclude[e].asString());
        }
        binding.argument = trigger["argument"].asInt(0);
        eosc::Binding::Mode mode;
        binding.mode = eosc::Binding::parseMode(item["mode"].asString("value"), mode) ? mode : eosc::Binding::Mode::Value;
        const JsonValue& range = item["range"];
        binding.low = range["low"].asFloat(0.0f);
        binding.high = range["high"].asFloat(1.0f);
        binding.threshold = range["threshold"].asFloat(0.5f);
        const JsonValue& limit = item["limit"];
        binding.minInterval = limit["interval"].asNumber(1.0 / 30.0);
        binding.minChange = limit["change"].asNumber(0.01);
        binding.maxInterval = limit["keepalive"].asNumber(0.2);
        binding.enabled = item["enabled"].asBool(true);

        // A native binding lands in its property's own range (ecore::landUnit),
        // so the map's params.low/high, which python's `param` scales by, are
        // not read here: the shipped maps write the knob's range there.
        const std::string action = item["action"].asString("param");
        const JsonValue& params = item["params"];
        if (action == "audio")
        {
            AudioChannel channel;
            std::string name = params["channel"].asString("");
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (findAudioChannel(name, channel))
            {
                binding.property = std::string("audio.") + audioChannelName(channel);
                bus.add(binding);
                continue;
            }
        }
        else if (action == "param" && !params["name"].asString("").empty())
        {
            const std::string layer = params["layer"].asString("");
            binding.property = (layer.empty() ? "" : layer + ".") + params["name"].asString("");
            looks.add(binding);
            continue;
        }
        rest.add(binding);
    }

    std::lock_guard<std::mutex> guard(lock);
    toBus = std::move(bus);
    toLooks = std::move(looks);
    reported = std::move(rest);
    arrived.clear();
    mapPath = path;
    outNative = static_cast<int>(toBus.size() + toLooks.size());
    outLoaded = outNative + static_cast<int>(reported.size());
    return true;
}

std::string OscInput::describe(double now) const
{
    std::lock_guard<std::mutex> guard(lock);
    const size_t native = toBus.size() + toLooks.size();
    int live = 0;
    for (const eosc::BindingSet* set : {&toBus, &toLooks, &reported})
    {
        for (const eosc::Binding& binding : set->all())
        {
            if (binding.enabled) ++live;
        }
    }
    std::ostringstream out;
    out << "listening=" << (running.load() ? "yes" : "no")
        << " host=" << (host.empty() ? "-" : host)
        << " port=" << port
        << " map=" << (mapPath.empty() ? "-" : mapPath)
        << " bindings=" << (native + reported.size()) << " live=" << live << " native=" << native
        << " packets=" << oscPort.getPackets() << " messages=" << messages.load()
        << " undecodable=" << oscPort.getUndecodable() << " fires=" << fires.load()
        << " addresses=" << seenByAddress.size();
    const double heard = lastHeardAt.load();
    if (heard < 0.0) out << " heard=never";
    else
    {
        char age[32];
        std::snprintf(age, sizeof(age), " heard=%.1fs", now - heard);
        out << age;
    }
    out << " watch=" << (watching.load() ? "on" : "off");
    return out.str();
}

// ---- output --------------------------------------------------------------------

OscOutput::~OscOutput()
{
    close();
}

bool OscOutput::pointAt(std::string& outError)
{
    std::string address;
    std::string hostNow;
    int portNow;
    {
        std::lock_guard<std::mutex> guard(lock);
        hostNow = host;
        portNow = port;
    }
    if (!enet::HostUdp::resolve(hostNow, portNow, address, outError))
    {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock);
    if (address == resolved && socket.isOpen())
    {
        return true;
    }
    if (!socket.point(hostNow, portNow, outError))
    {
        return false;
    }
    oscPort.setTransport(&socket);
    const bool moved = !resolved.empty() && resolved != address;
    resolved = socket.getRemoteAddress();
    if (moved)
    {
        lines.push_back("OSC-OUT resolved " + hostNow + " " + resolved);
    }
    return true;
}

bool OscOutput::open(const std::string& inEndpoint, std::string& outError, double inResolveEvery)
{
    close();
    std::string parsedHost;
    int parsedPort = 0;
    if (!parseOscEndpoint(inEndpoint, 6000, parsedHost, parsedPort, outError))
    {
        return false;
    }
    if (parsedPort == 0)
    {
        outError = "'" + inEndpoint + "': port 0 is nowhere to send";
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        endpoint = inEndpoint;
        host = parsedHost;
        port = parsedPort;
        isName = !enet::HostUdp::isLiteral(parsedHost);
        resolveEvery = inResolveEvery;
        resolved.clear();
        lastError.clear();
    }
    std::string error;
    const bool pointed = pointAt(error);
    if (!pointed && !isName)
    {
        outError = error;
        return false;
    }
    if (!pointed)
    {
        std::lock_guard<std::mutex> guard(lock);
        lastError = error;   // a name that has not answered yet: keep trying
    }
    opened.store(true);
    stopping.store(false);
    if (isName && resolveEvery > 0.0)
    {
        resolver = std::thread([this]() { watch(); });
    }
    return true;
}

void OscOutput::watch()
{
    double next = nowSeconds() + resolveEvery;
    while (!stopping.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (nowSeconds() < next) continue;
        next = nowSeconds() + resolveEvery;
        std::string error;
        if (!pointAt(error))
        {
            std::lock_guard<std::mutex> guard(lock);
            lastError = error;
        }
    }
}

void OscOutput::close()
{
    stopping.store(true);
    if (resolver.joinable()) resolver.join();
    opened.store(false);
    std::lock_guard<std::mutex> guard(lock);
    oscPort.setTransport(nullptr);
    socket.close();
    resolved.clear();
}

void OscOutput::record(const eosc::Message& message, bool delivered)
{
    if (delivered) sentCount.fetch_add(1); else droppedCount.fetch_add(1);
    unsigned long long overflow = 0;
    recordSeen(sentByAddress, overflow, 128, message, nowSeconds());
}

bool OscOutput::send(const eosc::Message& message)
{
    if (!opened.load()) return false;
    std::lock_guard<std::mutex> guard(lock);
    const bool delivered = socket.isOpen() && oscPort.send(message);
    if (!delivered && !socket.isOpen() && lastError.empty())
    {
        lastError = "not resolved yet";
    }
    record(message, delivered);
    return delivered;
}

bool OscOutput::sendColor(int r, int g, int b)
{
    const float inGamma = gamma.load();
    auto normalise = [inGamma](int channel)
    {
        float value = std::clamp(static_cast<float>(channel) / 255.0f, 0.0f, 1.0f);
        if (inGamma > 0.0f) value = std::pow(value, 1.0f / inGamma);
        return value;
    };
    const float red = normalise(r);
    const float green = normalise(g);
    const float blue = normalise(b);
    const std::string address = getControl();
    if (separate.load())
    {
        bool all = true;
        all = send(eosc::Message(address + "/r").add(red)) && all;
        all = send(eosc::Message(address + "/g").add(green)) && all;
        all = send(eosc::Message(address + "/b").add(blue)) && all;
        return all;
    }
    return send(eosc::Message(address).add(red).add(green).add(blue));
}

std::vector<std::string> OscOutput::drain()
{
    std::lock_guard<std::mutex> guard(lock);
    std::vector<std::string> out;
    out.swap(lines);
    return out;
}

std::vector<OscSeen> OscOutput::sent() const
{
    std::lock_guard<std::mutex> guard(lock);
    return snapshot(sentByAddress);
}

void OscOutput::forget()
{
    std::lock_guard<std::mutex> guard(lock);
    sentByAddress.clear();
}

std::string OscOutput::describe() const
{
    std::lock_guard<std::mutex> guard(lock);
    std::ostringstream out;
    out << "open=" << (opened.load() ? "yes" : "no")
        << " endpoint=" << (endpoint.empty() ? "-" : endpoint)
        << " address=" << (resolved.empty() ? "-" : resolved)
        << " control=" << control
        << " form=" << (separate.load() ? "separate" : "rgb")
        << " gamma=" << number(gamma.load())
        << " tap=" << tapFixture.load()
        << " sent=" << sentCount.load() << " dropped=" << droppedCount.load();
    if (!lastError.empty()) out << " error=" << jsonString(lastError);
    return out.str();
}
