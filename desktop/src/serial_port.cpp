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
    #include <grp.h>
    #include <pwd.h>
    #include <string.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <termios.h>
    #include <unistd.h>
    #include <vector>
#endif

using namespace edmx;

// ============================================================================
// TimeCriticalSection
// ============================================================================

#if defined(_WIN32)

TimeCriticalSection::TimeCriticalSection()
{
    HANDLE thread = ::GetCurrentThread();
    previousPriority = ::GetThreadPriority(thread);
    if (previousPriority != THREAD_PRIORITY_ERROR_RETURN)
    {
        raised = ::SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL) != 0;
    }
}

TimeCriticalSection::~TimeCriticalSection()
{
    if (raised)
    {
        ::SetThreadPriority(::GetCurrentThread(), previousPriority);
    }
}

#else

// Raising priority on posix wants either root or a configured rtprio limit, and
// failing that quietly is worse than not trying: a show that needs it can be
// started under `chrt`. Left as a no-op so the call sites read the same.
TimeCriticalSection::TimeCriticalSection() = default;
TimeCriticalSection::~TimeCriticalSection() = default;

#endif

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

bool SerialPort::open(const std::string& path, int baud, std::string& outError, int stopBits, bool assertDtr)
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
    // See the header: a DMX widget wants DTR left alone, a CDC device needs it
    // asserted before it will believe anyone is listening.
    dcb.fDtrControl     = assertDtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
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

int SerialPort::readAvailable(uint8_t* out, size_t capacity)
{
    if (!isOpen() || !out || capacity == 0)
    {
        return isOpen() ? 0 : -1;
    }

    // The timeouts set in open() - ReadIntervalTimeout MAXDWORD with both
    // totals at zero - are Win32's way of saying "return what is buffered and
    // do not wait", which is exactly what a show loop can afford.
    DWORD read = 0;
    if (!::ReadFile(static_cast<HANDLE>(handle), out, static_cast<DWORD>(capacity), &read, nullptr))
    {
        return -1;
    }
    return static_cast<int>(read);
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

bool SerialPort::touchAt1200(const std::string& path, std::string& outError)
{
    const std::string full = "\\\\.\\" + path;

    HANDLE h = ::CreateFileA(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        outError = "could not open " + path + ": " + lastWindowsError();
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (::GetCommState(h, &dcb))
    {
        dcb.BaudRate = 1200;
        dcb.ByteSize = 8;
        dcb.Parity   = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        ::SetCommState(h, &dcb);
    }

    // The board watches for the rate change *and* DTR going away, so drop it
    // explicitly rather than trusting CloseHandle to do it.
    ::EscapeCommFunction(h, CLRDTR);
    ::Sleep(50);
    ::CloseHandle(h);

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
    /// Turns "Permission denied" into the thing you actually have to do.
    ///
    /// This is the single most common way a Linux rig fails to light, it has
    /// nothing to do with the widget or the cable, and `errno 13` on its own
    /// sends people looking at both. The node is almost always owned by a group
    /// - `uucp` or `dialout` depending on the distro - that the user is simply
    /// not in, so say which group, say whether they are in it, and say the
    /// command.
    ///
    /// Worth the lookup at the exact moment a show has just failed to come up:
    /// nobody debugging this at a venue should have to know what udev did.
    std::string describeAccessDenial(const std::string& path)
    {
        struct stat node;
        if (::stat(path.c_str(), &node) != 0)
        {
            return std::string();
        }

        std::string owner = std::to_string(node.st_gid);
        if (const group* entry = ::getgrgid(node.st_gid))
        {
            owner = entry->gr_name;
        }

        // Do we hold that group already? If so the problem is something else -
        // a stale login whose groups predate the usermod is the usual answer,
        // and "log out and back in" is a different instruction from "run this".
        bool member = false;
        const int count = ::getgroups(0, nullptr);
        if (count > 0)
        {
            std::vector<gid_t> held(static_cast<size_t>(count));
            if (::getgroups(count, held.data()) == count)
            {
                member = std::find(held.begin(), held.end(), node.st_gid) != held.end();
            }
        }

        if (member)
        {
            return ". This user is already in group '" + owner + "', so the shell running"
                   " eclipse-dmx has a login older than that change - log out and back in";
        }

        std::string user = "$USER";
        if (const passwd* who = ::getpwuid(::getuid()))
        {
            user = who->pw_name;
        }

        return ". " + path + " belongs to group '" + owner + "' and " + user + " is not in it."
               " Permanently: sudo usermod -aG " + owner + " " + user + ", then log out and back"
               " in. Right now, until the widget is unplugged: sudo chown " + user + " " + path;
    }

    /// Maps a baud rate to its termios constant.
    ///
    /// **DMX's 250000 is not in here**, on any platform, and that is not an
    /// oversight: the table goes 230400 then jumps straight to 460800, so the
    /// one rate this program most needs is the one with no constant to name it
    /// by. See setCustomBaud() for how it actually gets set.
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
#ifdef B460800
            case 460800: outSpeed = B460800; return true;
#endif
#ifdef B921600
            case 921600: outSpeed = B921600; return true;
#endif
            default: return false;
        }
    }

#if defined(__linux__)

    constexpr bool kCustomBaudSupported = true;

    /// The kernel's `struct termios2`, declared here rather than included.
    ///
    /// `<asm/termbits.h>` is where it lives, and it brings its own `struct
    /// termios` with it — which collides head-on with the `<termios.h>` the
    /// rest of this file is built on. Two definitions of the same name, one
    /// translation unit. So it is written out instead. This is kernel ABI and
    /// does not move.
    ///
    /// Note `c_cc[19]`, where glibc's termios has 32. That difference is
    /// exactly the kind of thing that makes including both a bad idea, and
    /// exactly the kind of thing that would corrupt the stack quietly if it
    /// were got wrong here.
    struct Termios2
    {
        unsigned int  c_iflag;
        unsigned int  c_oflag;
        unsigned int  c_cflag;
        unsigned int  c_lflag;
        unsigned char c_line;
        unsigned char c_cc[19];
        unsigned int  c_ispeed;
        unsigned int  c_ospeed;
    };

    /// Sets a baud rate that has no constant, by handing the kernel the number.
    ///
    /// This is the whole reason DMX works on Linux at all. `BOTHER` in the
    /// speed field means "the rate is in c_ospeed", and TCSETS2 is the ioctl
    /// that carries it. Called *after* tcsetattr, and it reads the current
    /// state back first, so everything already configured survives — this
    /// changes the speed and nothing else.
    ///
    /// The FTDI in an open widget divides a 3MHz clock, so 250000 is exact
    /// rather than approximated. Nothing here has to care, but it is the reason
    /// the wire is reliable at a rate the table never listed.
    bool setCustomBaud(int fd, int baud, std::string& outError)
    {
        // Built from _IOR/_IOW rather than the constants they expand to, so the
        // encoding is right for whatever this is compiled for.
        const unsigned long kGetTermios2 = _IOR('T', 0x2A, Termios2);
        const unsigned long kSetTermios2 = _IOW('T', 0x2B, Termios2);

        constexpr unsigned int kBOther = 0010000; ///< "the rate is a number"
        constexpr unsigned int kCBaud  = 0010017; ///< the speed field, incl. CBAUDEX

        Termios2 tty{};
        if (::ioctl(fd, kGetTermios2, &tty) != 0)
        {
            outError = "baud " + std::to_string(baud) + " has no constant and TCGETS2 failed: "
                     + lastPosixError();
            return false;
        }

        tty.c_cflag &= ~kCBaud;
        tty.c_cflag |= kBOther;
        tty.c_ispeed = static_cast<unsigned int>(baud);
        tty.c_ospeed = static_cast<unsigned int>(baud);

        if (::ioctl(fd, kSetTermios2, &tty) != 0)
        {
            outError = "could not set baud " + std::to_string(baud) + ": " + lastPosixError()
                     + " (the driver may not do arbitrary rates)";
            return false;
        }

        // Read it back, because the ioctl succeeding is not the same as the
        // rate landing. A driver is free to accept BOTHER and then clamp to
        // whatever its divisor can reach, and a DMX line running at the wrong
        // speed does not report an error — it just sits there dark while
        // everything upstream says it is working. Which is exactly the failure
        // that is impossible to diagnose from the other end of the room.
        Termios2 landed{};
        if (::ioctl(fd, kGetTermios2, &landed) == 0
            && landed.c_ospeed != static_cast<unsigned int>(baud))
        {
            outError = "asked for baud " + std::to_string(baud) + " and the driver settled on "
                     + std::to_string(landed.c_ospeed);
            return false;
        }
        return true;
    }

#else

    // macOS has its own answer to this (IOSSIOSPEED); nothing here needs it
    // yet, so an unlisted rate is still refused there.
    constexpr bool kCustomBaudSupported = false;

    bool setCustomBaud(int, int, std::string&) { return false; }

#endif
}

bool SerialPort::isOpen() const
{
    return fd >= 0;
}

bool SerialPort::open(const std::string& path, int baud, std::string& outError, int stopBits, bool assertDtr)
{
    close();

    const int opened = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (opened < 0)
    {
        const int why = errno;
        outError = "could not open " + path + ": " + lastPosixError();
        if (why == EACCES)
        {
            outError += describeAccessDenial(path);
        }
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
    // A pure poll: return whatever has arrived, immediately, and never wait.
    // This was VTIME 5 back when nothing here read at all; a relic talks back
    // over the same cable now, and half a second inside a 25ms frame budget is
    // not a timeout, it is a stall.
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    speed_t speed = B115200;
    const bool standardBaud = baudToSpeed(baud, speed);

    if (standardBaud)
    {
        if (::cfsetispeed(&tty, speed) != 0 || ::cfsetospeed(&tty, speed) != 0)
        {
            outError = "could not set baud " + std::to_string(baud) + " on " + path + ": " + lastPosixError();
            ::close(opened);
            return false;
        }
    }
    else if (!kCustomBaudSupported)
    {
        outError = "baud rate " + std::to_string(baud) + " is not available on this platform";
        ::close(opened);
        return false;
    }
    // Otherwise the rate goes in below, after the flags have landed. It cannot
    // go in here: there is no constant to put in the struct.

    if (::tcsetattr(opened, TCSANOW, &tty) != 0)
    {
        outError = "tcsetattr failed on " + path + ": " + lastPosixError();
        ::close(opened);
        return false;
    }

    if (!standardBaud && !setCustomBaud(opened, baud, outError))
    {
        outError += " (on " + path + ")";
        ::close(opened);
        return false;
    }

    // See the header. A CDC device needs DTR before it will believe the port
    // belongs to anyone; a widget would rather we kept off the line.
    {
        int bits = 0;
        if (::ioctl(opened, TIOCMGET, &bits) == 0)
        {
            if (assertDtr) { bits |= TIOCM_DTR; }
            else           { bits &= ~TIOCM_DTR; }
            ::ioctl(opened, TIOCMSET, &bits);
        }
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

int SerialPort::readAvailable(uint8_t* out, size_t capacity)
{
    if (!isOpen() || !out || capacity == 0)
    {
        return isOpen() ? 0 : -1;
    }

    // VMIN 0 / VTIME 0 from open(): take what is buffered and return.
    const ssize_t got = ::read(fd, out, capacity);
    if (got < 0)
    {
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
    }
    return static_cast<int>(got);
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

bool SerialPort::touchAt1200(const std::string& path, std::string& outError)
{
    const int opened = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (opened < 0)
    {
        outError = "could not open " + path + ": " + lastPosixError();
        return false;
    }

    termios tty{};
    if (::tcgetattr(opened, &tty) == 0)
    {
        ::cfmakeraw(&tty);
        ::cfsetispeed(&tty, B1200);
        ::cfsetospeed(&tty, B1200);
        ::tcsetattr(opened, TCSANOW, &tty);
    }

    // The board watches for the rate change *and* DTR going away.
    int bits = 0;
    if (::ioctl(opened, TIOCMGET, &bits) == 0)
    {
        bits &= ~TIOCM_DTR;
        ::ioctl(opened, TIOCMSET, &bits);
    }

    usleep(50 * 1000);
    ::close(opened);

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
