// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "packet.h"

#include <cstring>

using namespace eosc;

namespace
{
    constexpr char kBundleTag[] = "#bundle";

    size_t padded(size_t length)
    {
        return (length + 4) & ~static_cast<size_t>(3);   // room for the terminator, then to a multiple of four
    }

    uint32_t readU32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
             | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    }

    uint64_t readU64(const uint8_t* p)
    {
        return (static_cast<uint64_t>(readU32(p)) << 32) | readU32(p + 4);
    }

    void writeU32(uint8_t* p, uint32_t value)
    {
        p[0] = static_cast<uint8_t>(value >> 24);
        p[1] = static_cast<uint8_t>(value >> 16);
        p[2] = static_cast<uint8_t>(value >> 8);
        p[3] = static_cast<uint8_t>(value);
    }

    void writeU64(uint8_t* p, uint64_t value)
    {
        writeU32(p, static_cast<uint32_t>(value >> 32));
        writeU32(p + 4, static_cast<uint32_t>(value));
    }

    /// A null-terminated, four-byte-padded string at `offset`. False when
    /// there is no terminator inside the data.
    bool readString(const uint8_t* data, size_t length, size_t& offset, std::string& out)
    {
        const uint8_t* start = data + offset;
        const void* end = std::memchr(start, 0, length - offset);
        if (end == nullptr)
        {
            return false;
        }
        const size_t text = static_cast<const uint8_t*>(end) - start;
        out.assign(reinterpret_cast<const char*>(start), text);
        offset += padded(text);
        if (offset > length) offset = length;
        return true;
    }

    size_t writeString(uint8_t* out, const std::string& text)
    {
        const size_t total = padded(text.size());
        std::memcpy(out, text.data(), text.size());
        std::memset(out + text.size(), 0, total - text.size());
        return total;
    }

    bool isBundle(const uint8_t* data, size_t length)
    {
        return length >= 16 && std::memcmp(data, kBundleTag, 8) == 0;
    }
}

bool Packet::decodeOne(const uint8_t* data, size_t length, Message& outMessage)
{
    outMessage.address.clear();
    outMessage.args.clear();
    if (data == nullptr || length == 0)
    {
        return false;
    }

    size_t offset = 0;
    if (!readString(data, length, offset, outMessage.address) || outMessage.address.empty()
        || outMessage.address[0] != '/')
    {
        return false;
    }

    std::string tags;
    if (offset >= length || !readString(data, length, offset, tags) || tags.empty() || tags[0] != ',')
    {
        return true;   // an address with no type tags: a message with no arguments
    }

    for (size_t t = 1; t < tags.size(); ++t)
    {
        Arg arg;
        switch (tags[t])
        {
            case 'i':
                if (offset + 4 > length) return true;
                arg = Arg::Int(static_cast<int32_t>(readU32(data + offset)));
                offset += 4;
                break;
            case 'f':
            {
                if (offset + 4 > length) return true;
                const uint32_t bits = readU32(data + offset);
                float value;
                std::memcpy(&value, &bits, sizeof(value));
                arg = Arg::Float(value);
                offset += 4;
                break;
            }
            case 'd':
            {
                if (offset + 8 > length) return true;
                const uint64_t bits = readU64(data + offset);
                double value;
                std::memcpy(&value, &bits, sizeof(value));
                arg = Arg::Double(value);
                offset += 8;
                break;
            }
            case 's':
            {
                std::string text;
                if (offset >= length || !readString(data, length, offset, text)) return true;
                arg = Arg::Str(text);
                break;
            }
            case 'b':
            {
                if (offset + 4 > length) return true;
                const uint32_t size = readU32(data + offset);
                offset += 4;
                if (size > length - offset) return true;
                arg = Arg::Blob(std::string(reinterpret_cast<const char*>(data + offset), size));
                offset += (size + 3) & ~static_cast<size_t>(3);
                break;
            }
            case 'T': arg = Arg::Bool(true); break;
            case 'F': arg = Arg::Bool(false); break;
            case 'N': arg = Arg::NilValue(); break;
            case 'I': arg = Arg::ImpulseValue(); break;
            default:
                return true;   // a tag this decoder does not know: keep what was read
        }
        outMessage.args.push_back(arg);
    }
    return true;
}

int Packet::decode(const uint8_t* data, size_t length, MessageSink& sink, double when)
{
    if (data == nullptr || length == 0)
    {
        return 0;
    }
    if (isBundle(data, length))
    {
        int count = 0;
        size_t offset = 16;   // "#bundle\0" and the timetag
        while (offset + 4 <= length)
        {
            const uint32_t size = readU32(data + offset);
            offset += 4;
            if (size > length - offset)
            {
                break;
            }
            count += decode(data + offset, size, sink, when);
            offset += size;
        }
        return count;
    }

    Message message;
    if (!decodeOne(data, length, message))
    {
        return 0;
    }
    sink.onMessage(message, when);
    return 1;
}

size_t Packet::encodedSize(const Message& message)
{
    size_t total = padded(message.address.size()) + padded(message.args.size() + 1);
    for (const Arg& arg : message.args)
    {
        switch (arg.type)
        {
            case Arg::Type::Int32:
            case Arg::Type::Float32: total += 4; break;
            case Arg::Type::Float64: total += 8; break;
            case Arg::Type::String:  total += padded(arg.bytes.size()); break;
            case Arg::Type::Blob:    total += 4 + ((arg.bytes.size() + 3) & ~static_cast<size_t>(3)); break;
            default: break;
        }
    }
    return total;
}

size_t Packet::encode(const Message& message, uint8_t* out, size_t capacity)
{
    const size_t needed = encodedSize(message);
    if (out == nullptr || needed > capacity || message.address.empty())
    {
        return 0;
    }

    size_t offset = writeString(out, message.address);
    offset += writeString(out + offset, message.typeTags());
    for (const Arg& arg : message.args)
    {
        switch (arg.type)
        {
            case Arg::Type::Int32:
                writeU32(out + offset, static_cast<uint32_t>(arg.i));
                offset += 4;
                break;
            case Arg::Type::Float32:
            {
                uint32_t bits;
                std::memcpy(&bits, &arg.f, sizeof(bits));
                writeU32(out + offset, bits);
                offset += 4;
                break;
            }
            case Arg::Type::Float64:
            {
                uint64_t bits;
                std::memcpy(&bits, &arg.d, sizeof(bits));
                writeU64(out + offset, bits);
                offset += 8;
                break;
            }
            case Arg::Type::String:
                offset += writeString(out + offset, arg.bytes);
                break;
            case Arg::Type::Blob:
            {
                const size_t size = arg.bytes.size();
                const size_t total = (size + 3) & ~static_cast<size_t>(3);
                writeU32(out + offset, static_cast<uint32_t>(size));
                offset += 4;
                std::memcpy(out + offset, arg.bytes.data(), size);
                std::memset(out + offset + size, 0, total - size);
                offset += total;
                break;
            }
            default:
                break;   // T, F, N, I carry nothing
        }
    }
    return offset;
}

std::vector<uint8_t> Packet::encode(const Message& message)
{
    std::vector<uint8_t> out(encodedSize(message));
    const size_t written = encode(message, out.data(), out.size());
    out.resize(written);
    return out;
}
