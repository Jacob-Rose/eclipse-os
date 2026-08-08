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
