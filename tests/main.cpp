// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <cstring>

/// `eclipse-tests [substring]` - runs every case, or only the ones whose name
/// contains the substring.
int main(int argc, char** argv)
{
    if (argc > 1)
    {
        std::vector<etest::Case> kept;
        for (const etest::Case& test : etest::cases())
        {
            if (std::strstr(test.name, argv[1]) != nullptr)
            {
                kept.push_back(test);
            }
        }
        etest::cases() = kept;
    }
    return etest::runAll();
}
