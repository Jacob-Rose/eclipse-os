// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

///
/// Minimal blocking serial port, Win32 on one side and termios on the other.
///
/// The Enttec widgets are FTDI parts that present as a normal COM / tty, so a
/// plain write of a framed packet is the whole driver. We keep break control
/// around because the cheap Open DMX widget has no firmware and needs the host
/// to bang out the DMX break itself.
///

namespace edmx
{
    struct SerialPortInfo
    {
        std::string path;        ///< what you pass to open(): "COM3", "/dev/ttyUSB0"
        std::string description; ///< best-effort friendly name, may be empty
    };

    /// Keeps the scheduler off our back for the length of a DMX frame.
    ///
    /// A host-timed DMX frame is a break, a mark, and then ~3ms of bytes that
    /// have to arrive without a gap the receiver will read as a new break. The
    /// host has no hardware doing this for it, so if the OS preempts the thread
    /// mid-frame the fixtures see a malformed one and blink. That is the whole
    /// reason a firmware widget is worth having.
    ///
    /// We cannot stop preemption, only make it less likely, so the send runs at
    /// raised priority and drops straight back. Scoped rather than set once at
    /// startup: a process that sits at time-critical priority for its whole life
    /// is a bad neighbour, and the risky window here is only milliseconds long.
    class TimeCriticalSection
    {
    public:
        TimeCriticalSection();
        ~TimeCriticalSection();

        TimeCriticalSection(const TimeCriticalSection&) = delete;
        TimeCriticalSection& operator=(const TimeCriticalSection&) = delete;

    private:
        int previousPriority{0};
        bool raised{false};
    };

    class SerialPort
    {
    public:
        SerialPort() = default;
        ~SerialPort();

        SerialPort(const SerialPort&) = delete;
        SerialPort& operator=(const SerialPort&) = delete;

        /// Opens `path` at `baud`, 8 data bits, no parity, no flow control.
        /// `stopBits` is 1 for the PRO's framed protocol and 2 for raw DMX512.
        bool open(const std::string& path, int baud, std::string& outError, int stopBits = 1);
        void close();
        bool isOpen() const;

        /// Writes every byte or fails. Returns false and fills outError on a
        /// short write or a device that went away mid-show.
        bool write(const uint8_t* data, size_t length, std::string& outError);

        /// Takes whatever has already arrived, up to `capacity`, and returns
        /// how many bytes that was. Never waits: zero means "nothing yet", not
        /// an error.
        ///
        /// A relic talks back — its log lines and its answer to a Hello come up
        /// the same cable the frames go down — and a show loop cannot stop to
        /// listen. Returns -1 on a port that has actually failed.
        int readAvailable(uint8_t* out, size_t capacity);

        /// Holds the line low for `microseconds`, then releases it and idles
        /// (marks) for `markAfterMicroseconds`. Used only by the Open DMX path.
        bool sendBreak(int microseconds, int markAfterMicroseconds, std::string& outError);

        const std::string& getPath() const { return openPath; }

        /// Serial devices currently attached. Best effort, and deliberately
        /// unfiltered: we would rather show a port we cannot use than hide the
        /// one the user needs.
        static std::vector<SerialPortInfo> enumeratePorts();

    private:
        std::string openPath;

#if defined(_WIN32)
        void* handle{nullptr}; // HANDLE, kept as void* so windows.h stays out of this header
#else
        int fd{-1};
#endif
    };
}
