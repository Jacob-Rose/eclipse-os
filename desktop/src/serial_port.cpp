// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/serial_port.h"

#include <algorithm>
#include <chrono>
#include <thread>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <dirent.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <string.h>
    #include <sys/ioctl.h>
    #include <termios.h>
    #include <unistd.h>
#endif

using namespace edmx;

namespace
{
    /// Busy-waits for a few microseconds.
    ///
    /// Deliberately a spin rather than sleep_for. DMX's break is 92us and its
    /// mark-after-break 12us, while a Windows sleep rounds up to the scheduler
    /// tick — 1ms at best, often 15. Sleeping here does not merely make the
    /// break long, it makes it *variable*, and a receiver that cannot find a
    /// consistent break start reads the frame shifted.
    ///
    /// At 40fps this spins for well under a millisecond a second, which is a
    /// fair price for a frame that lands.
    void spinMicroseconds(int microseconds)
    {
        if (microseconds <= 0)
        {
            return;
        }

        const auto target = std::chrono::steady_clock::now()
                          + std::chrono::microseconds(microseconds);
        while (std::chrono::steady_clock::now() < target)
        {
            // nothing: the wait *is* the work
        }
    }

#if defined(_WIN32)
    std::string lastWindowsError()
    {
        const DWORD code = ::GetLastError();
        if (code == 0)
        {
            return "no error";
        }

        LPSTR buffer = nullptr;
        const DWORD length = ::FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

        std::string message = (length && buffer) ? std::string(buffer, length) : ("error " + std::to_string(code));
        if (buffer)
        {
            ::LocalFree(buffer);
        }

        while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
        {
            message.pop_back();
        }
        return message + " (code " + std::to_string(code) + ")";
    }
#else
    std::string lastPosixError()
    {
        return std::string(::strerror(errno)) + " (errno " + std::to_string(errno) + ")";
    }
#endif
}

SerialPort::~SerialPort()
{
    close();
}

// ============================================================================
// Windows
// ============================================================================
#if defined(_WIN32)

bool SerialPort::isOpen() const
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

bool SerialPort::open(const std::string& path, int baud, std::string& outError, int stopBits)
{
    close();

    // COM10 and up need the \\.\ prefix; harmless on the low ones so always use it.
    const std::string devicePath = (path.rfind("\\\\.\\", 0) == 0) ? path : ("\\\\.\\" + path);

    HANDLE h = ::CreateFileA(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        outError = "could not open " + path + ": " + lastWindowsError();
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!::GetCommState(h, &dcb))
    {
        outError = "GetCommState failed on " + path + ": " + lastWindowsError();
        ::CloseHandle(h);
        return false;
    }

    dcb.BaudRate        = static_cast<DWORD>(baud);
    dcb.ByteSize        = 8;
    dcb.Parity          = NOPARITY;
    dcb.StopBits        = (stopBits >= 2) ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary         = TRUE;
    dcb.fParity         = FALSE;
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    // Leave DTR and RTS alone rather than asserting them. Neither line means
    // anything to a DMX widget, but plenty of FTDI-based boards wire one of
    // them to a reset or to the RS485 driver-enable, where asserting it holds
    // the thing mute. Nothing downstream needs them, so do not drive them.
    dcb.fDtrControl     = DTR_CONTROL_DISABLE;
    dcb.fRtsControl     = RTS_CONTROL_DISABLE;
    dcb.fOutX           = FALSE;
    dcb.fInX            = FALSE;
    dcb.fAbortOnError   = FALSE;
    dcb.fDsrSensitivity = FALSE;

    if (!::SetCommState(h, &dcb))
    {
        outError = "SetCommState failed on " + path + " (baud " + std::to_string(baud) + "): " + lastWindowsError();
        ::CloseHandle(h);
        return false;
    }

    // Never block forever on a write: a wedged widget should surface as an
    // error we can report, not a hung show.
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.WriteTotalTimeoutConstant   = 500;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    ::SetCommTimeouts(h, &timeouts);

    ::PurgeComm(h, PURGE_TXCLEAR | PURGE_RXCLEAR);

    handle = h;
    openPath = path;
    return true;
}

void SerialPort::close()
{
    if (isOpen())
    {
        ::CloseHandle(static_cast<HANDLE>(handle));
    }
    handle = nullptr;
    openPath.clear();
}

bool SerialPort::write(const uint8_t* data, size_t length, std::string& outError)
{
    if (!isOpen())
    {
        outError = "serial port is not open";
        return false;
    }

    size_t written = 0;
    while (written < length)
    {
        DWORD chunk = 0;
        if (!::WriteFile(static_cast<HANDLE>(handle), data + written,
                         static_cast<DWORD>(length - written), &chunk, nullptr))
        {
            outError = "write to " + openPath + " failed: " + lastWindowsError();
            return false;
        }
        if (chunk == 0)
        {
            outError = "write to " + openPath + " timed out";
            return false;
        }
        written += chunk;
    }
    return true;
}

bool SerialPort::sendBreak(int microseconds, int markAfterMicroseconds, std::string& outError)
{
    if (!isOpen())
    {
        outError = "serial port is not open";
        return false;
    }

    // Push out anything still queued first, exactly as the posix path does with
    // tcdrain. Without this the break is asserted while the previous frame is
    // still draining out of the driver, which truncates that frame and moves
    // the break to a place no receiver expects. On a dumb FTDI cable that is
    // the difference between a rig that renders and a rig that strobes.
    ::FlushFileBuffers(static_cast<HANDLE>(handle));

    if (!::SetCommBreak(static_cast<HANDLE>(handle)))
    {
        outError = "SetCommBreak failed: " + lastWindowsError();
        return false;
    }
    spinMicroseconds(microseconds);

    if (!::ClearCommBreak(static_cast<HANDLE>(handle)))
    {
        outError = "ClearCommBreak failed: " + lastWindowsError();
        return false;
    }
    spinMicroseconds(markAfterMicroseconds);
    return true;
}

std::vector<SerialPortInfo> SerialPort::enumeratePorts()
{
    std::vector<SerialPortInfo> ports;

    // HARDWARE\DEVICEMAP\SERIALCOMM is the cheapest reliable listing: one value
    // per present port, mapping driver name -> COMn.
    HKEY key = nullptr;
    if (::RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return ports;
    }

    for (DWORD index = 0;; ++index)
    {
        char nameBuffer[512];
        BYTE dataBuffer[512];
        DWORD nameSize = sizeof(nameBuffer);
        DWORD dataSize = sizeof(dataBuffer);
        DWORD valueType = 0;

        const LONG result = ::RegEnumValueA(key, index, nameBuffer, &nameSize, nullptr,
                                            &valueType, dataBuffer, &dataSize);
        if (result == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        if (result != ERROR_SUCCESS || valueType != REG_SZ)
        {
            continue;
        }

        SerialPortInfo info;
        info.path = std::string(reinterpret_cast<const char*>(dataBuffer));
        // trim the trailing NUL the registry hands back
        info.path.erase(std::find(info.path.begin(), info.path.end(), '\0'), info.path.end());
        info.description = std::string(nameBuffer, nameSize);
        if (!info.path.empty())
        {
            ports.push_back(info);
        }
    }

    ::RegCloseKey(key);
    return ports;
}

// ============================================================================
// POSIX (Linux, macOS)
// ============================================================================
#else

namespace
{
    /// Maps a baud rate to its termios constant. DMX's native 250000 only
    /// exists on Linux; elsewhere the caller has to fall back.
    bool baudToSpeed(int baud, speed_t& outSpeed)
    {
        switch (baud)
        {
            case 9600:   outSpeed = B9600;   return true;
            case 19200:  outSpeed = B19200;  return true;
            case 38400:  outSpeed = B38400;  return true;
            case 57600:  outSpeed = B57600;  return true;
            case 115200: outSpeed = B115200; return true;
            case 230400: outSpeed = B230400; return true;
#ifdef B250000
            case 250000: outSpeed = B250000; return true;
#endif
#ifdef B460800
            case 460800: outSpeed = B460800; return true;
#endif
#ifdef B921600
            case 921600: outSpeed = B921600; return true;
#endif
            default: return false;
        }
    }
}

bool SerialPort::isOpen() const
{
    return fd >= 0;
}

bool SerialPort::open(const std::string& path, int baud, std::string& outError, int stopBits)
{
    close();

    const int opened = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (opened < 0)
    {
        outError = "could not open " + path + ": " + lastPosixError();
        return false;
    }

    // Back to blocking now that open() has not stalled on carrier detect.
    const int flags = ::fcntl(opened, F_GETFL, 0);
    ::fcntl(opened, F_SETFL, flags & ~O_NONBLOCK);

    termios tty{};
    if (::tcgetattr(opened, &tty) != 0)
    {
        outError = "tcgetattr failed on " + path + ": " + lastPosixError();
        ::close(opened);
        return false;
    }

    cfmakeraw(&tty);
    tty.c_cflag |= (CLOCAL | CREAD);
    if (stopBits >= 2)
    {
        tty.c_cflag |= CSTOPB;   // raw DMX512 is 8N2
    }
    else
    {
        tty.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);
    }
    tty.c_cflag &= ~static_cast<tcflag_t>(PARENB);
    tty.c_cflag &= ~static_cast<tcflag_t>(CSIZE);
    tty.c_cflag |= CS8;
#ifdef CRTSCTS
    tty.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
#endif
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5; // 0.5s read timeout; we mostly write

    speed_t speed = B115200;
    if (!baudToSpeed(baud, speed))
    {
        outError = "baud rate " + std::to_string(baud) + " is not available on this platform";
        ::close(opened);
        return false;
    }
    if (::cfsetispeed(&tty, speed) != 0 || ::cfsetospeed(&tty, speed) != 0)
    {
        outError = "could not set baud " + std::to_string(baud) + " on " + path + ": " + lastPosixError();
        ::close(opened);
        return false;
    }

    if (::tcsetattr(opened, TCSANOW, &tty) != 0)
    {
        outError = "tcsetattr failed on " + path + ": " + lastPosixError();
        ::close(opened);
        return false;
    }

    ::tcflush(opened, TCIOFLUSH);

    fd = opened;
    openPath = path;
    return true;
}

void SerialPort::close()
{
    if (fd >= 0)
    {
        ::close(fd);
    }
    fd = -1;
    openPath.clear();
}

bool SerialPort::write(const uint8_t* data, size_t length, std::string& outError)
{
    if (!isOpen())
    {
        outError = "serial port is not open";
        return false;
    }

    size_t written = 0;
    while (written < length)
    {
        const ssize_t chunk = ::write(fd, data + written, length - written);
        if (chunk < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            outError = "write to " + openPath + " failed: " + lastPosixError();
            return false;
        }
        written += static_cast<size_t>(chunk);
    }
    return true;
}

bool SerialPort::sendBreak(int microseconds, int markAfterMicroseconds, std::string& outError)
{
    if (!isOpen())
    {
        outError = "serial port is not open";
        return false;
    }

    // Push out anything queued first, otherwise the break lands mid-frame.
    ::tcdrain(fd);

    if (::ioctl(fd, TIOCSBRK, 0) < 0)
    {
        outError = "TIOCSBRK failed: " + lastPosixError();
        return false;
    }
    spinMicroseconds(microseconds);

    if (::ioctl(fd, TIOCCBRK, 0) < 0)
    {
        outError = "TIOCCBRK failed: " + lastPosixError();
        return false;
    }
    spinMicroseconds(markAfterMicroseconds);
    return true;
}

std::vector<SerialPortInfo> SerialPort::enumeratePorts()
{
    std::vector<SerialPortInfo> ports;

    // USB serial adapters land in a small, well-known set of names. Walking
    // /dev directly keeps us free of udev/libusb.
    static const char* const prefixes[] = {
        "ttyUSB",      // FTDI / CH340 on Linux
        "ttyACM",      // CDC-ACM on Linux
        "cu.usbserial",// FTDI on macOS
        "cu.usbmodem",
    };

    DIR* dir = ::opendir("/dev");
    if (!dir)
    {
        return ports;
    }

    while (dirent* entry = ::readdir(dir))
    {
        const std::string name(entry->d_name);
        for (const char* prefix : prefixes)
        {
            if (name.rfind(prefix, 0) == 0)
            {
                SerialPortInfo info;
                info.path = "/dev/" + name;
                info.description = name;
                ports.push_back(info);
                break;
            }
        }
    }

    ::closedir(dir);
    std::sort(ports.begin(), ports.end(),
              [](const SerialPortInfo& a, const SerialPortInfo& b) { return a.path < b.path; });
    return ports;
}

#endif
