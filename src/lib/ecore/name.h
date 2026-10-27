// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>
#include <string>

// No `using namespace std;` here: this header reaches every translation unit,
// and the arduino-pico core builds at gnu++23, where std has names (byte,
// lerp, size, data) that Arduino also defines. Qualify instead.

namespace ecore
{
    /* @brief A hashed string: an event's name, a trigger, a tag a look answers to.
    *
    * Compared by hash, so a lookup on every frame costs nothing. The hash is
    * FNV-1a 32-bit, computed here and constexpr, so it is the same number on
    * the host and on the relic and a name can cross the cable as a hash.
    *
    * Dots make a hierarchy: `scanner.ping` matches `scanner`, and the match is
    * on a boundary, so `scan` does not claim it. Declare the names a look
    * answers to once, beside the look, rather than spelling them inline:
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

        /// FNV-1a over the text; empty text hashes to kEmptyHash.
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

        /// This name or one of its children: `scanner.ping` matches `scanner`
        /// and itself; `scanner` does not match `scanner.ping`.
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

    /// The hierarchical use of a Name: a tag is a name with dots.
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
