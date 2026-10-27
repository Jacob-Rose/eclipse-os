// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

///
/// The whole test framework. Header-only and dependency-free on purpose: the
/// library compiles on the host with USE_ARDUINO=0, and that is the only
/// place its math can be exercised without a board on the bench.
///
///     TEST(hsv_blend_crosses_the_seam)
///     {
///         HSV a(350.f, 1.f, 1.f), b(10.f, 1.f, 1.f);
///         CHECK_NEAR(HSV::blend(a, b, 0.5f).getHueFloat(), 0.f, 0.1f);
///     }
///
/// A failing CHECK reports and carries on, so one run shows every failure in
/// a test. tests/run.sh builds every tests/*.cpp against the portable library
/// and runs them.
///

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace etest
{
    struct Case
    {
        const char* name;
        void (*run)();
    };

    inline std::vector<Case>& cases()
    {
        static std::vector<Case> all;
        return all;
    }

    inline int& failures()
    {
        static int count = 0;
        return count;
    }

    struct Registrar
    {
        Registrar(const char* name, void (*run)()) { cases().push_back(Case{name, run}); }
    };

    /// one line per failed check, with the values when the macro has them
    inline void fail(const char* file, int line, const char* expr, const std::string& detail)
    {
        ++failures();
        std::printf("  FAIL %s:%d  %s%s\n", file, line, expr, detail.c_str());
    }

    /// uint8_t would print as a character; everything numeric goes through here
    template <typename T>
    inline std::string show(const T& value)
    {
        return std::to_string(static_cast<long double>(value));
    }

    inline std::string show(bool value) { return value ? "true" : "false"; }
    inline std::string show(const std::string& value) { return "\"" + value + "\""; }
    inline std::string show(const char* value) { return std::string("\"") + value + "\""; }

    /// runs every registered case; the exit code for main()
    inline int runAll()
    {
        int failedCases = 0;
        for (const Case& test : cases())
        {
            const int before = failures();
            std::printf("%s\n", test.name);
            test.run();
            if (failures() != before)
            {
                ++failedCases;
            }
        }
        std::printf("\n%zu tests, %d failed\n", cases().size(), failedCases);
        return failedCases == 0 ? 0 : 1;
    }
}

#define TEST(name)                                                      \
    static void etest_##name();                                         \
    static etest::Registrar etest_reg_##name(#name, &etest_##name);     \
    static void etest_##name()

#define CHECK(expr)                                                     \
    do                                                                  \
    {                                                                   \
        if (!(expr))                                                    \
        {                                                               \
            etest::fail(__FILE__, __LINE__, #expr, "");                 \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do                                                                                    \
    {                                                                                     \
        const auto etest_a = (a);                                                         \
        const auto etest_b = (b);                                                         \
        if (!(etest_a == etest_b))                                                        \
        {                                                                                 \
            etest::fail(__FILE__, __LINE__, #a " == " #b,                                 \
                        "  (" + etest::show(etest_a) + " vs " + etest::show(etest_b) + ")"); \
        }                                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                             \
    do                                                                                    \
    {                                                                                     \
        const double etest_a = static_cast<double>(a);                                    \
        const double etest_b = static_cast<double>(b);                                    \
        if (std::fabs(etest_a - etest_b) > static_cast<double>(eps))                      \
        {                                                                                 \
            etest::fail(__FILE__, __LINE__, #a " ~= " #b,                                 \
                        "  (" + etest::show(etest_a) + " vs " + etest::show(etest_b) + ")"); \
        }                                                                                 \
    } while (0)
