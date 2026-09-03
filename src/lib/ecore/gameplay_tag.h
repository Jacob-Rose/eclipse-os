// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>
#include <string>

namespace ecore
{
    /* @brief A gameplay-facing identifier: dotted text, compared by hash.
    *
    * `scanner.ping`, `record.count`, `mushroom.found` - the names that gameplay
    * throws around: what a trigger is called, what a sound stands for, what
    * a look answers to. Text under the hood, because text is what travels
    * the protocol and what a human reads in a log; hashed, because a look
    * asks "is this mine?" on every impulse and should not be comparing
    * strings to find out. Unreal's FName / FGameplayTag split, at the size
    * this codebase needs: Name is the general hashed string (the event
    * manager keys on it), a GameplayTag is the one with a hierarchy.
    *
    * The hierarchy is the dots. `scanner.ping` matches `scanner`, so a
    * handler can take a family of tags without listing them, and it is a
    * boundary match, not a prefix one - `scan` does not claim `scanner.ping`.
    *
    * The hash is FNV-1a, 32 bits, computed here rather than borrowed from
    * std::hash on purpose: it is the same number on the host and on the
    * relic's RP2040, and constexpr, so a tag that crosses the USB cable as
    * a hash one day can be checked against a constant on the other end.
    * The text is kept alongside, so an ERR can say which tag it was.
    *
    * Declare the tags a look answers to once, next to the look, and compare
    * against those; a tag spelled inline at the compare site is a typo
    * waiting to happen:
    *
    *     namespace scanner_tags { inline const GameplayTag Ping{"scanner.ping"}; }
    *     ...
    *     if (tag != scanner_tags::Ping) return false;
    */
    class GameplayTag
    {
    public:
        static constexpr uint32_t kEmptyHash = 0;

        /// An empty tag: matches nothing, and isValid() is false.
        GameplayTag() = default;

        explicit GameplayTag(const char* inText) : text(inText), hash(hashOf(inText)) {}
        explicit GameplayTag(const std::string& inText) : text(inText), hash(hashOf(inText.c_str())) {}

        /// FNV-1a over the text. Empty text hashes to kEmptyHash, so an
        /// empty tag is the one tag with that hash.
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

        /// Exactly this tag.
        bool operator==(const GameplayTag& other) const { return hash == other.hash; }
        bool operator!=(const GameplayTag& other) const { return hash != other.hash; }

        /// This tag, or one of its children: `scanner.ping` matches `scanner`
        /// and matches itself; `scanner` does not match `scanner.ping`. A
        /// text walk rather than a hash compare - it runs when a family is
        /// asked for, which is rarer than an exact compare and cheap anyway.
        bool matches(const GameplayTag& parent) const
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

    static_assert(GameplayTag::hashOf("scanner.ping") != GameplayTag::hashOf("scanner.pong"),
                  "the hash has to tell two tags apart");
    static_assert(GameplayTag::hashOf("") == GameplayTag::kEmptyHash,
                  "an empty tag is the empty hash");
}
