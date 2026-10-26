// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

///
/// Host (desktop) platform shim.
///
/// Pulled in by core.h when USE_ARDUINO == 0. The portable slice of the library
/// (ecore / eanim / esm / eio::HSVStrip) only reaches for a couple of Arduino
/// globals, so rather than fork those files we supply the same names here and
/// let the exact same translation units build for Windows and Linux.
///
/// Nothing on the microcontroller path ever sees this header.
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
        /// milliseconds since the first call, mirroring Arduino's millis()
        inline uint32_t millis_since_start()
        {
            using clock = std::chrono::steady_clock;
            static const clock::time_point start = clock::now();
            const auto delta = clock::now() - start;
            return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count());
        }

        /// shared engine for the ecore::get_random_* helpers.
        /// deterministic by default so a show renders the same twice; seed it
        /// yourself if you want the jitter.
        inline std::mt19937& rng()
        {
            static std::mt19937 engine(0x1CE0FB5u);
            return engine;
        }

        inline void seed_rng(uint32_t seed)
        {
            rng().seed(seed);
        }

        /// Arduino's Serial goes to the usb console. On the host we send log
        /// traffic to stderr on purpose: stdout carries the machine-readable
        /// control protocol that the python wrapper speaks.
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
/// GPIO, enough of it to compile.
///
/// The relic sources describe their wiring in headers — pin numbers for
/// buttons, an analog pin for a strip — and those headers get pulled in by the
/// pattern sources we do want. There is no GPIO on a desktop, so rather than
/// fork the headers we give the names somewhere to land.
///
/// A host button always reads unpressed. That is honest: there is no button.
/// Anything the desktop wants to drive interactively is driven through the
/// control protocol instead, which is where a UI can reach it.
///
/// Constants, deliberately not macros. Arduino ships these as #defines, and a
/// macro named HIGH silently rewrites eio::EBrightness::HIGH into a numeric
/// literal wherever this header lands first. Same hazard for INPUT and OUTPUT
/// against any enum that uses those words. Real constants scope properly.
inline constexpr int A0 = 26;
inline constexpr int A1 = 27;
inline constexpr int A2 = 28;
inline constexpr int A3 = 29;

inline constexpr int INPUT_PULLUP = 2;

inline void pinMode(uint8_t, int) {}
inline void digitalWrite(uint8_t, int) {}

/// Idle-high, matching a pull-up with nothing wired to it. Button reads this
/// as "not pressed", which is the truth on a desktop.
inline int digitalRead(uint8_t) { return 1; }
