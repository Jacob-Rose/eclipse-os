// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "address.h"

#include <cctype>

using namespace eosc;

namespace
{
    char fold(char c, bool foldCase)
    {
        return foldCase ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c;
    }

    bool matchFrom(const char* p, const char* s, bool foldCase);

    /// `[...]` at p: advances p past the set and says whether `c` is in it.
    bool matchSet(const char*& p, char c, bool foldCase)
    {
        ++p;   // the [
        bool negate = false;
        if (*p == '!' || *p == '^')
        {
            negate = true;
            ++p;
        }
        bool found = false;
        bool first = true;
        while (*p != '\0' && (*p != ']' || first))
        {
            first = false;
            char low = fold(*p, foldCase);
            char high = low;
            if (p[1] == '-' && p[2] != '\0' && p[2] != ']')
            {
                high = fold(p[2], foldCase);
                p += 2;
            }
            if (c >= low && c <= high)
            {
                found = true;
            }
            ++p;
        }
        if (*p == ']')
        {
            ++p;
        }
        return found != negate;
    }

    /// `{a,b}` at p: tries each alternative against the rest of the pattern.
    bool matchAlternatives(const char* p, const char* s, bool foldCase)
    {
        const char* close = p;
        while (*close != '\0' && *close != '}') ++close;
        if (*close == '\0')
        {
            return false;   // no closing brace: not a pattern this understands
        }
        const char* rest = close + 1;
        const char* start = p + 1;
        while (true)
        {
            const char* end = start;
            while (end < close && *end != ',') ++end;
            // the alternative, then the rest of the pattern, as one pattern
            std::string joined(start, static_cast<size_t>(end - start));
            joined.append(rest);
            if (matchFrom(joined.c_str(), s, foldCase))
            {
                return true;
            }
            if (end == close)
            {
                return false;
            }
            start = end + 1;
        }
    }

    bool matchFrom(const char* p, const char* s, bool foldCase)
    {
        while (*p != '\0')
        {
            switch (*p)
            {
                case '*':
                    while (*p == '*') ++p;
                    if (*p == '\0') return true;
                    for (const char* t = s; ; ++t)
                    {
                        if (matchFrom(p, t, foldCase)) return true;
                        if (*t == '\0') return false;
                    }
                case '?':
                    if (*s == '\0') return false;
                    ++p;
                    ++s;
                    break;
                case '[':
                    if (*s == '\0') return false;
                    if (!matchSet(p, fold(*s, foldCase), foldCase)) return false;
                    ++s;
                    break;
                case '{':
                    return matchAlternatives(p, s, foldCase);
                default:
                    if (fold(*p, foldCase) != fold(*s, foldCase)) return false;
                    ++p;
                    ++s;
                    break;
            }
        }
        return *s == '\0';
    }
}

bool eosc::matchAddress(const std::string& pattern, const std::string& address, bool foldCase)
{
    return matchFrom(pattern.c_str(), address.c_str(), foldCase);
}
