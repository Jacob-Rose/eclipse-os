// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>
#include <string>

// No `using namespace std;` here, deliberately.
//
// A header that opens std at global scope opens it for every translation unit
// that includes it, and the arduino-pico core builds at -std=gnu++23 - where
// std has `byte`, `lerp`, `size`, `data` and a growing list of names Arduino
// also defines. The collisions read as errors deep inside SPI.h or Common.h,
// nowhere near the header that caused them. Qualify instead.

namespace ecore
{
    /* @brief A hashed string: an event's name, a trigger, a tag a look answers to.
    *
    * Text under the hood, because text is what travels the protocol and what
    * a human reads in a log; compared by hash, because an event manager keys
    * on it and a look asks "is this mine?" on every impulse, and neither
    * should be comparing strings to find out.
    *
    * The hash is FNV-1a, 32 bits, computed here rather than borrowed from
    * std::hash on purpose: it is the same number on the host and on the
    * relic's RP2040, and constexpr, so a name that crosses the USB cable as
    * a hash can be checked against a constant on the other end.
    *
    * Dots make a hierarchy. `scanner.ping` matches `scanner`, so a handler
    * can take a family of names without listing them, and it is a boundary
    * match, not a prefix one - `scan` does not claim `scanner.ping`.
    *
    * Declare the names a look answers to once, next to the look, and compare
    * against those; a name spelled inline at the compare site is a typo
    * waiting to happen:
    *
    *     namespace scanner_tags { inline const Name Ping{"scanner.ping"}; }
    *     ...
    *     if (tag != scanner_tags::Ping) return false;
    */
    class Name
    {
    public:
        static constexpr uint32_t kEmptyHash = 0;

        /// An empty name: matches nothing, and isValid() is false.
        Name() = default;

        explicit Name(const char* inText) : text(inText), hash(hashOf(inText)) {}
        explicit Name(const std::string& inText) : text(inText), hash(hashOf(inText.c_str())) {}

        /// FNV-1a over the text. Empty text hashes to kEmptyHash, so an
        /// empty name is the one name with that hash.
        static constexpr uint32_t hashOf(const char* s)
        {
            if (s == nullptr || *s == '\0')
            {
                return kEmptyHash;
            }
            uint32_t h = 2166136261u;
            for (; *s != '\0'; ++s)
            {
                h ^= static_cast<uint8_t>(*s);
                h *= 16777619u;
            }
            return h;
        }

        const std::string& getText() const { return text; }
        uint32_t getHash() const { return hash; }
        bool isValid() const { return hash != kEmptyHash; }

        /// Exactly this name.
        bool operator==(const Name& other) const { return hash == other.hash; }
        bool operator!=(const Name& other) const { return hash != other.hash; }

        /// By text, for a sorted container or a stable listing.
        bool operator<(const Name& other) const { return text < other.text; }

        /// This name, or one of its children: `scanner.ping` matches `scanner`
        /// and matches itself; `scanner` does not match `scanner.ping`. A
        /// text walk rather than a hash compare - it runs when a family is
        /// asked for, which is rarer than an exact compare and cheap anyway.
        bool matches(const Name& parent) const
        {
            if (!isValid() || !parent.isValid())
            {
                return false;
            }
            if (hash == parent.hash)
            {
                return true;
            }
            const std::string& p = parent.text;
            return text.size() > p.size()
                && text.compare(0, p.size(), p) == 0
                && text[p.size()] == '.';
        }

    private:
        std::string text;
        uint32_t hash{kEmptyHash};
    };

    /// The hierarchical use of a Name. Same type: a tag is a name with dots.
    using GameplayTag = Name;

    // the hash contract, checked where it is defined
    static_assert(Name::hashOf("") == Name::kEmptyHash,
                  "an empty name is the empty hash");
    static_assert(Name::hashOf("a") == 0xe40c292cu,
                  "FNV-1a reference vector: the number has to be the same on every target");
    static_assert(Name::hashOf("a.b") != Name::hashOf("a.c"),
                  "the hash has to tell two names apart");
    static_assert(Name::hashOf("ab") != Name::hashOf("ba"),
                  "the hash has to see order, not just content");
}
