// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

///
/// Host (desktop) platform shim: the few Arduino names the portable library
/// uses, so the same sources build on Windows and Linux. Pulled in by core.h
/// when USE_ARDUINO == 0; the microcontroller build never sees it.
///

#include <cstdint>
#include <cstdio>
#include <chrono>
#include <random>
#include <string>

namespace ecore
{
    namespace platform
    {
        /// milliseconds since the first call, like Arduino's millis()
        inline uint32_t millis_since_start()
        {
            using clock = std::chrono::steady_clock;
            static const clock::time_point start = clock::now();
            const auto delta = clock::now() - start;
            return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count());
        }

        /// the engine behind ecore::get_random_*. Fixed seed, so a show
        /// renders the same twice; seed_rng() for jitter.
        inline std::mt19937& rng()
        {
            static std::mt19937 engine(0x1CE0FB5u);
            return engine;
        }

        inline void seed_rng(uint32_t seed)
        {
            rng().seed(seed);
        }

        /* @brief Serial on the host: writes to stderr.
        *
        * stdout is the control protocol the python wrapper reads, so logs must
        * stay off it.
        */
        struct HostSerial
        {
            void print(const char* msg)   { std::fputs(msg, stderr); }
            void print(const std::string& msg) { std::fputs(msg.c_str(), stderr); }
            void print(int val)           { std::fprintf(stderr, "%d", val); }
            void print(unsigned int val)  { std::fprintf(stderr, "%u", val); }
            void print(float val)         { std::fprintf(stderr, "%f", static_cast<double>(val)); }
            void print(char val)          { std::fputc(val, stderr); }

            void println(const char* msg) { std::fputs(msg, stderr); std::fputc('\n', stderr); std::fflush(stderr); }
            void println(const std::string& msg) { println(msg.c_str()); }
            void println()                { std::fputc('\n', stderr); std::fflush(stderr); }

            void flush()                  { std::fflush(stderr); }
            int  available()              { return 0; }
            int  read()                   { return -1; }
        };

        inline HostSerial& host_serial()
        {
            static HostSerial instance;
            return instance;
        }
    }
}

// Arduino-compatible spellings, so the shared sources need no #ifdefs.
#define Serial (::ecore::platform::host_serial())

inline uint32_t millis() { return ::ecore::platform::millis_since_start(); }

///
/// GPIO, enough to compile the relic wiring headers. There is no GPIO on a
/// desktop: pins do nothing and a button always reads unpressed. Constants
/// rather than Arduino's macros, so names like HIGH and INPUT cannot clobber
/// enum members.
///
inline constexpr int A0 = 26;
inline constexpr int A1 = 27;
inline constexpr int A2 = 28;
inline constexpr int A3 = 29;

inline constexpr int INPUT_PULLUP = 2;

inline void pinMode(uint8_t, int) {}
inline void digitalWrite(uint8_t, int) {}

/// idle-high, as a pull-up with nothing wired reads: not pressed
inline int digitalRead(uint8_t) { return 1; }
