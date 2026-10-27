// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>

namespace eosc
{
    /* @brief Whether an address matches a glob.
    *
    * `*` any run of characters, `?` one, `[abc]` and `[a-z]` a set, `[!a]`
    * its complement, `{bass,mid}` alternatives. Case is folded by default,
    * because the one thing known about the addresses a visualiser sends is
    * that they are derived from names and the derivation has changed before.
    * `*` crosses slashes: `*BassLevel*` finds the band wherever the app
    * nests it.
    *
    *     matchAddress("*basslevel*", "/syn/audio/BassLevel")   // true
    */
    bool matchAddress(const std::string& pattern, const std::string& address, bool foldCase = true);
}
